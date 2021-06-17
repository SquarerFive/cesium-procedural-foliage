// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Cesium3DTileset.h"
#include "FoliageCaptureActor.h"

#include "ProceduralFoliageEllipsoid.generated.h"

/**
 * 
 */
UCLASS()
class AIDEN_GEO_TUTORIAL_API AProceduralFoliageEllipsoid : public ACesium3DTileset
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Foliage")
		AFoliageCaptureActor* FoliageCaptureActor;

	virtual void Tick(float DeltaSeconds) override;

protected:
	// Initial spawn
	bool bHasFoliageSpawned = false;
};
