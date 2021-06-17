// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "FoliageHISM.generated.h"

/**
 * 
 */
UCLASS()
class AIDEN_GEO_TUTORIAL_API UFoliageHISM : public UHierarchicalInstancedStaticMeshComponent
{
	friend class UInstancedStaticMeshComponent;
	GENERATED_BODY()
public:
	UPROPERTY()
	TArray<FTransform> Transforms;

	UPROPERTY()
	bool bMarkedForAdd = false;

	UPROPERTY()
	bool bMarkedForClear = false;
};
