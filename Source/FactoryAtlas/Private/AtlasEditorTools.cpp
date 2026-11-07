#include "AtlasEditorTools.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"

bool UAtlasEditorTools::ConnectCustomizedUV(UMaterial* Material, UMaterialExpression* Expression, int32 Index)
{
#if WITH_EDITOR
	if (!Material || !Expression || Index < 0 || Index > 7)
	{
		return false;
	}
	FExpressionInput* Input = Material->GetExpressionInputForProperty(EMaterialProperty(MP_CustomizedUVs0 + Index));
	if (!Input)
	{
		return false;
	}
	Input->Connect(0, Expression);
	return true;
#else
	return false;
#endif
}
