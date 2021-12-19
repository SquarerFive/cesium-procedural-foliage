// Fill out your copyright notice in the Description page of Project Settings.


#include "FoliageCaptureActor.h"

#include "Kismet/KismetMathLibrary.h"

// Sets default values
AFoliageCaptureActor::AFoliageCaptureActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
}

// Called when the game starts or when spawned
void AFoliageCaptureActor::BeginPlay()
{
	Super::BeginPlay();
	ResetAndCreateHISMComponents();

	FCoreDelegates::PreWorldOriginOffset.AddLambda([&](UWorld* World, FIntVector CurrentOrigin, FIntVector NewOrigin) {
		bIsRebasing = true;
		
		WorldOffset = FVector(NewOrigin - CurrentOrigin);
		});

	FCoreDelegates::PostWorldOriginOffset.AddLambda([&](UWorld* World, FIntVector CurrentOrigin, FIntVector NewOrigin) {
		bIsRebasing = false;
		WorldOffset = FVector(0.);
	});
}

// Called every frame
void AFoliageCaptureActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (Ticks > UpdateFoliageAfterNumFrames && !bIsBuilding)
	{
		Ticks = 0;
		int32 ComponentsUpdated = 0;

		for (TPair<FFoliageGeometryType, TArray<UFoliageHISM*>>& FoliageHISMPair : HISMFoliageMap)
		{
			for (UFoliageHISM* FoliageHISM : FoliageHISMPair.Value)
			{
				// if (!IsValid(FoliageHISM)) { continue; }
				if (FoliageHISM->bMarkedForClear)
				{
#if !FOLIAGE_REDUCE_FLICKER_APPROACH_ENABLED
					FoliageHISM->ClearInstances();
					FoliageHISM->bCleared = true;
					FoliageHISM->bMarkedForClear = false;
					ComponentsUpdated++;
#endif
				}
				else if (FoliageHISM->bMarkedForAdd)
				{
#if FOLIAGE_REDUCE_FLICKER_APPROACH_ENABLED
					FoliageHISM->ClearInstances();
#endif
					FoliageHISM->PreAllocateInstancesMemory(FoliageHISM->Transforms.Num());
					FoliageHISM->AddInstances(FoliageHISM->Transforms, false);
					FoliageHISM->bMarkedForAdd = false;
					FoliageHISM->Transforms.Empty();
					FoliageHISM->bCleared = false;
					ComponentsUpdated++;
				}
				if (ComponentsUpdated > MaxComponentsToUpdatePerFrame)
				{
					break;
				}
			}
			if (ComponentsUpdated > MaxComponentsToUpdatePerFrame)
			{
				break;
			}
		}

		if (IsValid(Georeference)) {
			if (AllISMsMarkedAsCleared() && !bInstancesClearedCalled && IsWaiting()) {
				OnInstancesCleared();
			}
		}
	}

	Ticks++;
}

void AFoliageCaptureActor::BuildFoliageTransforms(UTextureRenderTarget2D* FoliageDistributionMap,
	UTextureRenderTarget2D* NormalAndDepthMap, FBox RTWorldBounds)
{
	// Need to check whether the CesiumGeoreference actor and input RTs are valid.
	if (!IsValid(Georeference))
	{
		UE_LOG(LogTemp, Warning, TEXT("Georeference is invalid! Not spawning in foliage"));
		return;
	}
	if (!IsValid(NormalAndDepthMap) || !IsValid(FoliageDistributionMap))
	{
		UE_LOG(LogTemp, Warning, TEXT("Invaid inputs for FoliageCaptureActor!"));
		return;
	}
	if (FoliageTypes.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No foliage types added!"));
		return;
	}

	bIsBuilding = true;

	// Find the geographic bounds of the RT
	const glm::dvec3 MinGeographic = Georeference->TransformUnrealToLongitudeLatitudeHeight(
		glm::dvec3(
			RTWorldBounds.Min.X,
			RTWorldBounds.Min.Y,
			RTWorldBounds.Min.Z
		));

	const glm::dvec3 MaxGeographic = Georeference->TransformUnrealToLongitudeLatitudeHeight(
		glm::dvec3(
			RTWorldBounds.Max.X,
			RTWorldBounds.Max.Y,
			RTWorldBounds.Max.Z
		)
	);
	const glm::dvec4 GeographicExtents2D = glm::dvec4(
		MinGeographic.x, MinGeographic.y,
		MaxGeographic.x, MaxGeographic.y
	);

	// Setup pixel extraction
	const int32 TotalPixels = FoliageDistributionMap->SizeX * FoliageDistributionMap->SizeY;

	TArray<FLinearColor>* ClassificationPixels = new TArray<FLinearColor>();
	TArray<FLinearColor>* NormalPixels = new TArray<FLinearColor>();

	FOnRenderTargetRead OnRenderTargetRead;
	OnRenderTargetRead.BindLambda(
		[this, FoliageDistributionMap, ClassificationPixels, NormalPixels, GeographicExtents2D, TotalPixels](
			bool bSuccess) mutable
		{
			if (!bSuccess)
			{
				bIsBuilding = false;
				return;
			}

			const int32 Width = FoliageDistributionMap->SizeX;

			TMap<UFoliageHISM*, int32> NewInstanceCountMap;
			FFoliageTransforms FoliageTransforms;

			for (int Index = 0; Index < TotalPixels; ++Index)
			{
				// Get the 2D pixel coordinates.
				const double X = Index % Width;
				const double Y = Index / Width;

				// Extract classification, normals and depth from the pixel arrays.
				const FLinearColor Classification = (*ClassificationPixels)[Index];
				const FLinearColor NormalDepth = (*NormalPixels)[Index];
				// Convert the RGB channel in the NormalDepth array to a FVector
				FVector Normal = FVector(NormalDepth.R, NormalDepth.G, NormalDepth.B);
				// Project the Alpha channel in NormalDepth to elevation (in metres) 
				const double Elevation = GetHeightFromDepth(NormalDepth.A);

				// Project pixel coords to geographic.
				const glm::dvec3 GeographicCoords = PixelToGeographicLocation(X, Y, Elevation, FoliageDistributionMap,
					GeographicExtents2D);
				// Then project to UE world coordinates
				const glm::dvec3 EngineCoords = Georeference->TransformLongitudeLatitudeHeightToUnreal(GeographicCoords);

				// Compute east north up
				const FMatrix EastNorthUpEngine = Georeference->InaccurateComputeEastNorthUpToUnreal(
					FVector(EngineCoords.x, EngineCoords.y, EngineCoords.z));



				for (FFoliageClassificationType& FoliageType : FoliageTypes)
				{
					// If classification pixel colour matches the classification of FoliageType
					if (Classification == FoliageType.ColourClassification)
					{
						bool bHasDoneRaycast = false;
						FVector Location = FVector(EngineCoords.x, EngineCoords.y, EngineCoords.z);

						if (FoliageType.bAlignToSurfaceWithRaycast)
						{
							CorrectFoliageTransform(Location, EastNorthUpEngine, Location, Normal, bHasDoneRaycast);
						}
						// Iterate through the mesh types inside FoliageType
						for (FFoliageGeometryType& FoliageGeometryType : FoliageType.FoliageTypes)
						{
							if (FMath::FRand() >= FoliageGeometryType.Density)
							{
								continue;
							}

							// Find rotation and scale
							const float Scale = FoliageGeometryType.Scale.Interpolate(FMath::FRand());
							FRotator Rotation;

							if (FoliageGeometryType.bAlignToNormal)
							{
								Rotation = UKismetMathLibrary::MakeRotFromZ(Normal);
							}
							else
							{
								Rotation = EastNorthUpEngine.Rotator();
							}

							// Apply a random angle to the rotation yaw if RandomYaw is true.
							if (FoliageGeometryType.bRandomYaw)
							{
								Rotation = UKismetMathLibrary::RotatorFromAxisAndAngle(
									Rotation.Quaternion().GetUpVector(), FMath::FRandRange(
										0.0, 360.0
									));
							}

							// Find HISM with minimum amount of transforms.

							UFoliageHISM* MinimumHISM = HISMFoliageMap[FoliageGeometryType][0];
							for (UFoliageHISM* HISM : HISMFoliageMap[FoliageGeometryType])
							{
								if (FoliageTransforms.HISMTransformMap.Contains(HISM) && FoliageTransforms.HISMTransformMap.Contains(MinimumHISM))
								{
									if (FoliageTransforms.HISMTransformMap[HISM].Num() < FoliageTransforms.HISMTransformMap[MinimumHISM].Num())
									{
										MinimumHISM = HISM;
									}
								}
							}
							if (!IsValid(MinimumHISM))
							{
								UE_LOG(LogTemp, Error, TEXT("MinimumHISM is invalid!"));
							}

							Location += WorldOffset;

							// Add our transform, and make it relative to the actor.
							FTransform NewTransform = FTransform(
								Rotation,
								Location + (Rotation.Quaternion().
									GetUpVector() * FoliageGeometryType.ZOffset.
									Interpolate(FMath::FRand())), FVector(Scale)
							).GetRelativeTransform(GetTransform());

							if (NewTransform.IsRotationNormalized())
							{
								if (!FoliageTransforms.HISMTransformMap.Contains(MinimumHISM))
								{
									FoliageTransforms.HISMTransformMap.Add(MinimumHISM, TArray{ NewTransform });
								}
								else
								{
									FoliageTransforms.HISMTransformMap[MinimumHISM].Add(NewTransform);
								}
							}
						}
					}
				}
			}
			delete ClassificationPixels;
			delete NormalPixels;
			ClassificationPixels = nullptr;
			NormalPixels = nullptr;

			AsyncTask(ENamedThreads::GameThread, [FoliageTransforms, this]()
				{
					// Marked for add
					for (const TPair<UFoliageHISM*, TArray<FTransform>>& Pair : FoliageTransforms.HISMTransformMap)
					{
						Pair.Key->Transforms.Append(Pair.Value);
						Pair.Key->bMarkedForAdd = true;
					}
					bIsBuilding = false;
				});
		});
	// Extract the pixels from the render targets, calling OnRenderTargetRead when complete.
	ReadLinearColorPixelsAsync(OnRenderTargetRead, TArray<FTextureRenderTargetResource*>{
		FoliageDistributionMap->GameThread_GetRenderTargetResource(),
			NormalAndDepthMap->GameThread_GetRenderTargetResource()
	}, TArray<TArray<FLinearColor>*>{
		ClassificationPixels, NormalPixels
	});
}

void AFoliageCaptureActor::ClearFoliageInstances()
{
	// Ensure the transforms array on the HISMs are cleared before building.
	for (TPair<FFoliageGeometryType, TArray<UFoliageHISM*>>& FoliageHISMPair : HISMFoliageMap)
	{
		for (UFoliageHISM* FoliageHISM : FoliageHISMPair.Value)
		{
			if (IsValid(FoliageHISM))
			{
				FoliageHISM->Transforms.Empty();
				FoliageHISM->bMarkedForClear = true;
			}
		}
	}
}

void AFoliageCaptureActor::ResetAndCreateHISMComponents()
{
	for (FFoliageClassificationType& FoliageType : FoliageTypes)
	{
		for (FFoliageGeometryType& FoliageGeometryType : FoliageType.FoliageTypes)
		{
			if (FoliageGeometryType.Mesh == nullptr) { continue; }
			if (HISMFoliageMap.Contains(FoliageGeometryType))
			{
				for (UFoliageHISM* HISM : HISMFoliageMap[FoliageGeometryType])
				{
					if (IsValid(HISM))
					{
						HISM->DestroyComponent();
					}
				}
				HISMFoliageMap[FoliageGeometryType].Empty();
			}
			HISMFoliageMap.Remove(FoliageGeometryType);

			for (int32 i = 0; i < FoliageType.PooledHISMsToCreatePerFoliageType; ++i)
			{
				UFoliageHISM* HISM = NewObject<UFoliageHISM>(this);
				HISM->SetupAttachment(GetRootComponent());
				HISM->RegisterComponent();

				HISM->SetStaticMesh(FoliageGeometryType.Mesh);
				HISM->SetCollisionEnabled(FoliageGeometryType.bCollidesWithWorld
					? ECollisionEnabled::QueryAndPhysics
					: ECollisionEnabled::NoCollision);
				HISM->SetCullDistances(FoliageGeometryType.CullingDistances.Min, FoliageGeometryType.CullingDistances.Max);

				// This may cause a slight hitch when enabled.
				HISM->bAffectDistanceFieldLighting = FoliageGeometryType.bAffectsDistanceFieldLighting;
				if (!HISMFoliageMap.Contains(FoliageGeometryType))
				{
					HISMFoliageMap.Add(FoliageGeometryType, TArray<UFoliageHISM*>{HISM});
				}
				else
				{
					HISMFoliageMap[FoliageGeometryType].Add(HISM);
				}
			}
		}
	}
}

void AFoliageCaptureActor::OnUpdate_Implementation(const FVector& NewLocation)
{
	// Align the actor to face the planet surface.
	// SetActorLocation(NewLocation);
	NewActorLocation = NewLocation;
	bInstancesClearedCalled = false;
	

	const FRotator PlanetAlignedRotation = Georeference->InaccurateComputeEastNorthUpToUnreal(NewLocation).Rotator();

	SetActorRotation(
		PlanetAlignedRotation
	);

	// Get grid min and max coords.
	const int32 Size = GridSize.X > 0 ? GridSize.X : 1;
	const FVector Start = GetActorTransform().TransformPosition(FVector(-(CaptureWidth * Size) / 2, 0, 0));
	const FVector End = GetActorTransform().TransformPosition(FVector((CaptureWidth * Size) / 2, 0, 0));

	// Find the distance (in degrees) between the grid min and max.
	glm::dvec3 GeoStart = Georeference->TransformUnrealToLongitudeLatitudeHeight(VectorToDVector(Start));
	glm::dvec3 GeoEnd = Georeference->TransformUnrealToLongitudeLatitudeHeight(VectorToDVector(End));

	GeoStart.z = CaptureElevation;
	GeoEnd.z = CaptureElevation;

	CaptureWidthInDegrees = glm::distance(GeoStart, GeoEnd) / 2;

	bIsWaiting = true;
	
#if FOLIAGE_REDUCE_FLICKER_APPROACH_ENABLED
	OnInstancesCleared();
#else
	ClearFoliageInstances();
#endif
}

void AFoliageCaptureActor::OnInstancesCleared_Implementation()
{
	if (NewActorLocation.IsSet()) {
		glm::dvec3 GeoPosition = Georeference->TransformUnrealToLongitudeLatitudeHeight(
			VectorToDVector(*NewActorLocation)
		);
		GeoPosition.z = CaptureElevation;
		glm::dvec3 EnginePosition = Georeference->TransformLongitudeLatitudeHeightToUnreal(GeoPosition);
		FVector EnginePositionVector = FVector(EnginePosition.x, EnginePosition.y, EnginePosition.z);
		ActorOffset = EnginePositionVector - GetActorLocation();

		SetActorLocation(
			EnginePositionVector
		);

#if FOLIAGE_REDUCE_FLICKER_APPROACH_ENABLED
		OffsetAllInstances(ActorOffset);
#endif

		NewActorLocation.Reset();
		bIsWaiting = false;
	}
}

bool AFoliageCaptureActor::IsBuilding() const
{
	return bIsBuilding;
}

bool AFoliageCaptureActor::IsWaiting() const
{
	return bIsWaiting;
}

void AFoliageCaptureActor::CorrectFoliageTransform(const FVector& InEngineCoordinates, const FMatrix& InEastNorthUp,
	FVector& OutCorrectedPosition, FVector& OutSurfaceNormals, bool& bSuccess) const
{
	UWorld* World = GetWorld();

	if (IsValid(World))
	{
		const FVector Up = InEastNorthUp.ToQuat().GetUpVector();
		FHitResult HitResult;

		World->LineTraceSingleByChannel(HitResult, InEngineCoordinates + (Up * 6000),
			InEngineCoordinates - (Up * 6000), ECollisionChannel::ECC_Visibility);

		if (HitResult.bBlockingHit)
		{
			bSuccess = true;
			OutCorrectedPosition = HitResult.ImpactPoint;
			OutSurfaceNormals = HitResult.ImpactNormal;
		}
	}
}

double AFoliageCaptureActor::GetHeightFromDepth(const double& Value) const
{
	return CaptureElevation - (1 - Value) / 0.00001 / 100;

}

glm::dvec3 AFoliageCaptureActor::PixelToGeographicLocation(const double& X, const double& Y, const double& Altitude,
	UTextureRenderTarget2D* RT,
	const glm::dvec4& GeographicExtents) const
{
	// Normalize the ranges of the coords
	const double AX = X / static_cast<double>(RT->SizeX);
	const double AY = Y / static_cast<double>(RT->SizeY);

	const double Long = FMath::Lerp<double>(
		GeographicExtents.x,
		GeographicExtents.z,
		1 - AY);
	const double Lat = FMath::Lerp<double>(
		GeographicExtents.y,
		GeographicExtents.w,
		AX);

	return glm::dvec3(Long, Lat, Altitude);
}

FIntPoint AFoliageCaptureActor::GeographicToPixelLocation(const double& Longitude, const double& Latitude,
	UTextureRenderTarget2D* RT,
	const glm::dvec4& GeographicExtents) const
{
	// Normalize long and lat
	const double LongitudeRange = GeographicExtents.z - GeographicExtents.x;
	const double LatitudeRange = GeographicExtents.w - GeographicExtents.y;
	const double ALongitude = (Longitude - GeographicExtents.x) / LongitudeRange;
	const double ALatitude = (Latitude - GeographicExtents.y) / LatitudeRange;

	const double X = FMath::Lerp<double>(0, RT->SizeX, ALatitude);
	const double Y = FMath::Lerp<double>(RT->SizeY, 0, ALongitude);

	return FIntPoint(X, Y);
}

void AFoliageCaptureActor::ReadLinearColorPixelsAsync(
	FOnRenderTargetRead OnRenderTargetRead,
	TArray<FTextureRenderTargetResource*> RTs,
	TArray<TArray<FLinearColor>*> OutImageData,
	FReadSurfaceDataFlags InFlags,
	FIntRect InRect,
	ENamedThreads::Type ExitThread)
{
	if (InRect == FIntRect(0, 0, 0, 0))
	{
		InRect = FIntRect(0, 0, RTs[0]->GetSizeXY().X, RTs[0]->GetSizeXY().Y);
	}


	struct FReadSurfaceContext
	{
		TArray<FTextureRenderTargetResource*> SrcRenderTargets;
		TArray<TArray<FLinearColor>*> OutData;
		FIntRect Rect;
		FReadSurfaceDataFlags Flags;
	};

	for (auto DT : OutImageData) { DT->Reset(); }
	FReadSurfaceContext Context =
	{
		RTs,
		OutImageData,
		InRect,
		InFlags
	};

	if (!Context.OutData[0])
	{
		UE_LOG(LogTemp, Error, TEXT("Buffer invalid!"));
		return;
	}

	ENQUEUE_RENDER_COMMAND(ReadSurfaceCommand)(
		[Context, OnRenderTargetRead, ExitThread](FRHICommandListImmediate& RHICmdList)
		{
			const FIntRect Rect = Context.Rect;
			const FReadSurfaceDataFlags Flags = Context.Flags;
			int i = 0;
			for (FRenderTarget* RT : Context.SrcRenderTargets)
			{
				const FTexture2DRHIRef& RefRenderTarget = RT->
					GetRenderTargetTexture();
				TArray<FLinearColor>* Buffer = Context.OutData[i];

				RHICmdList.ReadSurfaceData(
					RefRenderTarget,
					Rect,
					*Buffer,
					Flags
				);
				i++;
			}
			// instead of blocking the game thread, execute the delegate when finished.
			AsyncTask(
				ExitThread,
				[OnRenderTargetRead, Context]()
				{
					OnRenderTargetRead.Execute((*Context.OutData[0]).Num() > 0);
				});
		});
}

glm::dvec3 AFoliageCaptureActor::VectorToDVector(const FVector& InVector)
{
	return glm::dvec3(InVector.X, InVector.Y, InVector.Z);
}
