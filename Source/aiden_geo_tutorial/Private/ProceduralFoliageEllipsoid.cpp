// Fill out your copyright notice in the Description page of Project Settings.


#include "ProceduralFoliageEllipsoid.h"

#include "Kismet/GameplayStatics.h"

void AProceduralFoliageEllipsoid::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACesiumGeoreference* Geo = this->ResolveGeoreference();
	if (IsValid(Geo) && IsValid(FoliageCaptureActor))
	{
		// Don't start another build while the previous one is still running
		if (!FoliageCaptureActor->IsBuilding())
		{
			APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
			if (IsValid(CameraManager))
			{
				// Project the camera coordinates to geographic coordinates.
				const FVector CameraLocation = CameraManager->GetCameraLocation();
				glm::dvec3 GeographicCameraLocation = Geo->TransformUnrealToLongitudeLatitudeHeight(
					glm::dvec3(CameraLocation.X, CameraLocation.Y, CameraLocation.Z));

				// Keep original camera elevation as a variable, and swap z with the capture elevation (this will be the new capture location).
				const double CurrentCameraElevation = GeographicCameraLocation.z;
				GeographicCameraLocation.z = FoliageCaptureActor->CaptureElevation;

				// Ensure CesiumGeoreference is valid
				if (!IsValid(FoliageCaptureActor->Georeference))
				{
					FoliageCaptureActor->Georeference = Geo;
				}
			
				// Current geographic location of the foliage capture actor (used to measure the distance).
				const glm::dvec3 CurrentFoliageCaptureGeographicLocation = Geo->TransformUnrealToLongitudeLatitudeHeight(glm::dvec3(
					FoliageCaptureActor->GetActorLocation().X, FoliageCaptureActor->GetActorLocation().Y, FoliageCaptureActor->GetActorLocation().Z
				));
	
				const double Distance = glm::distance(GeographicCameraLocation, CurrentFoliageCaptureGeographicLocation);
				const double Speed = CameraManager->GetVelocity().Size();

				// New capture position
				const glm::dvec3 NewFoliageCaptureUELocation = Geo->TransformLongitudeLatitudeHeightToUnreal(GeographicCameraLocation);

				// Only update the foliage capture actor if the player is outside of the capture grid, within elevation and a speed less than 5000.
				if ((Distance > FoliageCaptureActor->CaptureWidthInDegrees && CurrentCameraElevation <= FoliageCaptureActor->CaptureElevation && Speed < 5000) || !bHasFoliageSpawned)
				{
					FoliageCaptureActor->OnUpdate(FVector(NewFoliageCaptureUELocation.x, NewFoliageCaptureUELocation.y, NewFoliageCaptureUELocation.z));
					bHasFoliageSpawned = true;
				}
			}
		}
	}
}
