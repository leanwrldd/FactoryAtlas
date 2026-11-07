#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AtlasEditorTools.generated.h"

class UMaterial;
class UMaterialExpression;

/// Python cannot name the hidden MP_CustomizedUVs enum entries, so create_assets.py wires them through here.
UCLASS()
class FACTORYATLAS_API UAtlasEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Factory Atlas")
	static bool ConnectCustomizedUV(UMaterial* Material, UMaterialExpression* Expression, int32 Index);
};
