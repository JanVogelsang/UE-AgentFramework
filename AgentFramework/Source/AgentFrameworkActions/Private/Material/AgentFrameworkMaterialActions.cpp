// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Material/AgentFrameworkMaterialActions.h"
#include "AgentFrameworkCoreModule.h"
#include "AgentFrameworkActionUtils.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "MaterialEditingLibrary.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Viewport/AgentFrameworkViewportActions.h"
#include "Editor.h"
#include "RenderingThread.h"
#include "Sound/SoundBase.h"

FAgentFrameworkMaterialActions::FAgentFrameworkMaterialActions() {}
FAgentFrameworkMaterialActions::~FAgentFrameworkMaterialActions() {}

FName FAgentFrameworkMaterialActions::GetActionName() const { return FName(TEXT("Material")); }

TArray<FString> FAgentFrameworkMaterialActions::GetSupportedToolNames() const
{
	return {
		TEXT("create_material"),
		TEXT("create_material_instance"),
		TEXT("add_material_expression"),
		TEXT("connect_material_property"),
		TEXT("capture_material"),
		TEXT("create_pbr_material_from_textures")
	};
}

bool FAgentFrameworkMaterialActions::ValidateParams(const TSharedRef<FJsonObject>& Params, TArray<FString>& OutErrors) const
{
	FString ToolName;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, OutErrors, false);

	if (ToolName == TEXT("create_pbr_material_from_textures") || ToolName == TEXT("material/create_pbr_from_textures"))
	{
		FString AssetPath;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("material_path"), AssetPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("MaterialPath"), AssetPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("AssetPath"), AssetPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("destination_path"), AssetPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("DestinationPath"), AssetPath, OutErrors, false))
		{
			OutErrors.Add(TEXT("Missing required parameter: material_path (or asset_path / destination_path)"));
			return false;
		}

		FString BaseColorPath;
		const TSharedPtr<FJsonObject>* TexMaps = nullptr;
		bool bHasTexMaps = (Params->TryGetObjectField(TEXT("texture_maps"), TexMaps) || Params->TryGetObjectField(TEXT("TextureMaps"), TexMaps)) && TexMaps && (*TexMaps).IsValid();

		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("base_color_texture_path"), BaseColorPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("BaseColorTexturePath"), BaseColorPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("base_color"), BaseColorPath, OutErrors, false) &&
			!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("BaseColor"), BaseColorPath, OutErrors, false) &&
			!(bHasTexMaps && (
				(*TexMaps)->TryGetStringField(TEXT("base_color_texture_path"), BaseColorPath) ||
				(*TexMaps)->TryGetStringField(TEXT("BaseColorTexturePath"), BaseColorPath) ||
				(*TexMaps)->TryGetStringField(TEXT("base_color"), BaseColorPath) ||
				(*TexMaps)->TryGetStringField(TEXT("BaseColor"), BaseColorPath)
			)))
		{
			OutErrors.Add(TEXT("Missing required parameter: base_color_texture_path (or base_color)"));
			return false;
		}

		return true;
	}

	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, OutErrors, true))
	{
		return false;
	}

	if (ToolName == TEXT("create_material_instance") || Params->HasField(TEXT("parent_material")))
	{
		FString ParentMaterial;
		if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("parent_material"), ParentMaterial, OutErrors, true))
		{
			return false;
		}
	}

	return true;
}

FAgentFrameworkActionResult FAgentFrameworkMaterialActions::ExecuteAction(const TSharedRef<FJsonObject>& Params)
{
	FString ToolName;
	TArray<FString> TempErrors;
	UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("_tool_name"), ToolName, TempErrors, false);

	bool bIsReadOnly = (ToolName == TEXT("capture_material"));

	TOptional<FScopedTransaction> Transaction;
	if (!bIsReadOnly)
	{
		Transaction.Emplace(FText::FromString(TEXT("AgentFramework Material Action")));
	}

	FAgentFrameworkActionResult Result;
	Result.bSuccess = false;

	if (ToolName == TEXT("create_material")) Result = ExecuteCreateMaterial(Params, Result);
	else if (ToolName == TEXT("create_material_instance")) Result = ExecuteCreateMaterialInstance(Params, Result);
	else if (ToolName == TEXT("add_material_expression")) Result = ExecuteAddMaterialExpression(Params, Result);
	else if (ToolName == TEXT("connect_material_property")) Result = ExecuteConnectMaterialProperty(Params, Result);
	else if (ToolName == TEXT("capture_material")) Result = ExecuteCaptureMaterial(Params, Result);
	else if (ToolName == TEXT("create_pbr_material_from_textures") || ToolName == TEXT("material/create_pbr_from_textures")) Result = ExecuteCreatePBRMaterialFromTextures(Params, Result);
	else if (Params->HasField(TEXT("parent_material")))
	{
		Result = ExecuteCreateMaterialInstance(Params, Result);
	}
	else if (Params->HasField(TEXT("expressions")))
	{
		Result = ExecuteCreateMaterial(Params, Result);
	}
	else
	{
		// Default: create a basic material
		Result = ExecuteCreateMaterial(Params, Result);
	}

	if (Result.bSuccess)
	{
		PlaySuccessSound();
	}

	if (Transaction.IsSet() && !Result.bSuccess)
	{
		Transaction->Cancel();
	}

	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMaterialActions::ExecuteCreateMaterial(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/Materials/M_Rock."),
			*AssetPath));
		return Result;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	if (!IsValid(Factory))
	{
		Result.Errors.Add(TEXT("Failed to create MaterialFactoryNew instance."));
		return Result;
	}

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);
	UMaterial* NewMaterial = Cast<UMaterial>(NewAsset);

	if (!IsValid(NewMaterial))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Material at %s"), *AssetPath));
		return Result;
	}

	// CRITICAL: Modify() before structural changes for undo support
	NewMaterial->Modify();

	FString BlendModeStr;
	if (UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("blend_mode"), BlendModeStr, Result.Errors, false) && !BlendModeStr.IsEmpty())
	{
		if (BlendModeStr.Equals(TEXT("BLEND_Opaque"), ESearchCase::IgnoreCase) || BlendModeStr.Equals(TEXT("Opaque"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Opaque;
		else if (BlendModeStr.Equals(TEXT("BLEND_Masked"), ESearchCase::IgnoreCase) || BlendModeStr.Equals(TEXT("Masked"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Masked;
		else if (BlendModeStr.Equals(TEXT("BLEND_Translucent"), ESearchCase::IgnoreCase) || BlendModeStr.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Translucent;
		else if (BlendModeStr.Equals(TEXT("BLEND_Additive"), ESearchCase::IgnoreCase) || BlendModeStr.Equals(TEXT("Additive"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Additive;
		else if (BlendModeStr.Equals(TEXT("BLEND_Modulate"), ESearchCase::IgnoreCase) || BlendModeStr.Equals(TEXT("Modulate"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_Modulate;
		else if (BlendModeStr.Equals(TEXT("BLEND_AlphaComposite"), ESearchCase::IgnoreCase) || BlendModeStr.Equals(TEXT("AlphaComposite"), ESearchCase::IgnoreCase)) NewMaterial->BlendMode = BLEND_AlphaComposite;
	}

	FString ShadingModelStr;
	if (UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("shading_model"), ShadingModelStr, Result.Errors, false) && !ShadingModelStr.IsEmpty())
	{
		if (ShadingModelStr.Equals(TEXT("MSM_Unlit"), ESearchCase::IgnoreCase) || ShadingModelStr.Equals(TEXT("Unlit"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_Unlit);
		else if (ShadingModelStr.Equals(TEXT("MSM_DefaultLit"), ESearchCase::IgnoreCase) || ShadingModelStr.Equals(TEXT("DefaultLit"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_DefaultLit);
		else if (ShadingModelStr.Equals(TEXT("MSM_Subsurface"), ESearchCase::IgnoreCase) || ShadingModelStr.Equals(TEXT("Subsurface"), ESearchCase::IgnoreCase)) NewMaterial->SetShadingModel(MSM_Subsurface);
	}

	// Process expressions if provided
	const TArray<TSharedPtr<FJsonValue>>* ExpressionsArray = nullptr;
	if (UAgentFrameworkActionUtils::TryGetArrayParam(Params, TEXT("expressions"), ExpressionsArray, Result.Errors, false) && ExpressionsArray)
	{
		TMap<FString, UMaterialExpression*> CreatedExpressions;

		for (const TSharedPtr<FJsonValue>& ExprValue : *ExpressionsArray)
		{
			if (!ExprValue.IsValid()) continue;
			const TSharedPtr<FJsonObject>& ExprObj = ExprValue->AsObject();
			if (!ExprObj.IsValid()) continue;

			FString ExprType;
			UAgentFrameworkActionUtils::TryGetStringParam(ExprObj, TEXT("type"), ExprType, Result.Errors, false);

			int32 PosX = 0;
			int32 PosY = 0;
			UAgentFrameworkActionUtils::TryGetIntParam(ExprObj, TEXT("x"), PosX, Result.Errors, false);
			UAgentFrameworkActionUtils::TryGetIntParam(ExprObj, TEXT("y"), PosY, Result.Errors, false);

			UMaterialExpression* Expression = nullptr;

			if (ExprType == TEXT("TextureSample"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionTextureSample::StaticClass(), PosX, PosY);
			}
			else if (ExprType == TEXT("Constant"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionConstant::StaticClass(), PosX, PosY);
				if (IsValid(Expression))
				{
					float Value = 0.0f;
					UAgentFrameworkActionUtils::TryGetFloatParam(ExprObj, TEXT("value"), Value, Result.Errors, false);
					if (UMaterialExpressionConstant* ConstExpr = Cast<UMaterialExpressionConstant>(Expression))
					{
						ConstExpr->R = Value;
					}
				}
			}
			else if (ExprType == TEXT("Constant3Vector") || ExprType == TEXT("Color"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionConstant3Vector::StaticClass(), PosX, PosY);
				if (IsValid(Expression))
				{
					if (UMaterialExpressionConstant3Vector* Const3Expr = Cast<UMaterialExpressionConstant3Vector>(Expression))
					{
						const TArray<TSharedPtr<FJsonValue>>* ColorArr = nullptr;
						if (ExprObj->TryGetArrayField(TEXT("default_value"), ColorArr) || ExprObj->TryGetArrayField(TEXT("value"), ColorArr))
						{
							FLinearColor Color = FLinearColor::White;
							if (ColorArr && ColorArr->Num() > 0) Color.R = (*ColorArr)[0]->AsNumber();
							if (ColorArr && ColorArr->Num() > 1) Color.G = (*ColorArr)[1]->AsNumber();
							if (ColorArr && ColorArr->Num() > 2) Color.B = (*ColorArr)[2]->AsNumber();
							if (ColorArr && ColorArr->Num() > 3) Color.A = (*ColorArr)[3]->AsNumber();
							Const3Expr->Constant = Color;
						}
					}
				}
			}
			else if (ExprType == TEXT("Multiply"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionMultiply::StaticClass(), PosX, PosY);
			}
			else if (ExprType == TEXT("Lerp") || ExprType == TEXT("LinearInterpolate"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionLinearInterpolate::StaticClass(), PosX, PosY);
			}
			else if (ExprType == TEXT("ScalarParameter"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionScalarParameter::StaticClass(), PosX, PosY);
				if (IsValid(Expression))
				{
					if (UMaterialExpressionScalarParameter* ScalarParamExpr = Cast<UMaterialExpressionScalarParameter>(Expression))
					{
						FString ParamName;
						if (UAgentFrameworkActionUtils::TryGetStringParam(ExprObj, TEXT("parameter_name"), ParamName, Result.Errors, false) && !ParamName.IsEmpty())
						{
							ScalarParamExpr->ParameterName = FName(*ParamName);
						}
						float DefaultVal = 0.0f;
						if (UAgentFrameworkActionUtils::TryGetFloatParam(ExprObj, TEXT("default_value"), DefaultVal, Result.Errors, false) ||
							UAgentFrameworkActionUtils::TryGetFloatParam(ExprObj, TEXT("value"), DefaultVal, Result.Errors, false))
						{
							ScalarParamExpr->DefaultValue = DefaultVal;
						}
					}
				}
			}
			else if (ExprType == TEXT("VectorParameter"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionVectorParameter::StaticClass(), PosX, PosY);
				if (IsValid(Expression))
				{
					if (UMaterialExpressionVectorParameter* VectorParamExpr = Cast<UMaterialExpressionVectorParameter>(Expression))
					{
						FString ParamName;
						if (UAgentFrameworkActionUtils::TryGetStringParam(ExprObj, TEXT("parameter_name"), ParamName, Result.Errors, false) && !ParamName.IsEmpty())
						{
							VectorParamExpr->ParameterName = FName(*ParamName);
						}
						const TArray<TSharedPtr<FJsonValue>>* ColorArr = nullptr;
						if (ExprObj->TryGetArrayField(TEXT("default_value"), ColorArr) || ExprObj->TryGetArrayField(TEXT("value"), ColorArr))
						{
							FLinearColor Color = FLinearColor::White;
							if (ColorArr && ColorArr->Num() > 0) Color.R = (*ColorArr)[0]->AsNumber();
							if (ColorArr && ColorArr->Num() > 1) Color.G = (*ColorArr)[1]->AsNumber();
							if (ColorArr && ColorArr->Num() > 2) Color.B = (*ColorArr)[2]->AsNumber();
							if (ColorArr && ColorArr->Num() > 3) Color.A = (*ColorArr)[3]->AsNumber();
							VectorParamExpr->DefaultValue = Color;
						}
					}
				}
			}
			else if (ExprType == TEXT("TextureCoordinate"))
			{
				Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionTextureCoordinate::StaticClass(), PosX, PosY);
			}

			// Dynamic expression class lookup fallback for ParticleColor or any other UMaterialExpression
			if (!Expression && !ExprType.IsEmpty())
			{
				FString CleanExprName = ExprType;
				if (!CleanExprName.StartsWith(TEXT("MaterialExpression")))
				{
					CleanExprName = TEXT("MaterialExpression") + CleanExprName;
				}
				UClass* ExprClass = UClass::TryFindTypeSlow<UClass>(*CleanExprName);
				if (!ExprClass)
				{
					ExprClass = FindObject<UClass>(nullptr, *CleanExprName);
				}
				if (ExprClass && ExprClass->IsChildOf(UMaterialExpression::StaticClass()))
				{
					Expression = UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, ExprClass, PosX, PosY);
				}
			}

			if (IsValid(Expression))
			{
				FString ExprId;
				if (UAgentFrameworkActionUtils::TryGetStringParam(ExprObj, TEXT("id"), ExprId, Result.Errors, false) ||
					UAgentFrameworkActionUtils::TryGetStringParam(ExprObj, TEXT("name"), ExprId, Result.Errors, false))
				{
					CreatedExpressions.Add(ExprId, Expression);
				}
				CreatedExpressions.Add(ExprType, Expression);

				FString ConnectTo;
				if (UAgentFrameworkActionUtils::TryGetStringParam(ExprObj, TEXT("connect_to"), ConnectTo, Result.Errors, false) && !ConnectTo.IsEmpty())
				{
					EMaterialProperty MatProp = MP_BaseColor;
					if (ConnectTo == TEXT("BaseColor")) MatProp = MP_BaseColor;
					else if (ConnectTo == TEXT("Metallic")) MatProp = MP_Metallic;
					else if (ConnectTo == TEXT("Specular")) MatProp = MP_Specular;
					else if (ConnectTo == TEXT("Roughness")) MatProp = MP_Roughness;
					else if (ConnectTo == TEXT("EmissiveColor") || ConnectTo == TEXT("Emissive")) MatProp = MP_EmissiveColor;
					else if (ConnectTo == TEXT("Normal")) MatProp = MP_Normal;
					else if (ConnectTo == TEXT("Opacity")) MatProp = MP_Opacity;
					else if (ConnectTo == TEXT("OpacityMask")) MatProp = MP_OpacityMask;

					FString OutputName;
					UAgentFrameworkActionUtils::TryGetStringParam(ExprObj, TEXT("output_name"), OutputName, Result.Errors, false);

					UMaterialEditingLibrary::ConnectMaterialProperty(Expression, OutputName, MatProp);
				}

				UE_LOG(LogAgentFramework, Log, TEXT("MaterialActions: Added expression %s at (%d, %d)"), *ExprType, PosX, PosY);
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Unknown or failed expression type: %s"), *ExprType));
			}
		}

		// Process inter-expression connections if provided
		const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
		if (UAgentFrameworkActionUtils::TryGetArrayParam(Params, TEXT("connections"), ConnectionsArray, Result.Errors, false) && ConnectionsArray)
		{
			for (const TSharedPtr<FJsonValue>& ConnVal : *ConnectionsArray)
			{
				if (!ConnVal.IsValid()) continue;
				const TSharedPtr<FJsonObject>& ConnObj = ConnVal->AsObject();
				if (!ConnObj.IsValid()) continue;

				FString FromName, ToName, FromOutput, ToInput, ToProp;
				UAgentFrameworkActionUtils::TryGetStringParam(ConnObj, TEXT("from"), FromName, Result.Errors, false);
				UAgentFrameworkActionUtils::TryGetStringParam(ConnObj, TEXT("from_output"), FromOutput, Result.Errors, false);
				UAgentFrameworkActionUtils::TryGetStringParam(ConnObj, TEXT("to"), ToName, Result.Errors, false);
				UAgentFrameworkActionUtils::TryGetStringParam(ConnObj, TEXT("to_input"), ToInput, Result.Errors, false);
				UAgentFrameworkActionUtils::TryGetStringParam(ConnObj, TEXT("to_property"), ToProp, Result.Errors, false);

				UMaterialExpression** FromExprPtr = CreatedExpressions.Find(FromName);
				if (!FromExprPtr || !IsValid(*FromExprPtr)) continue;

				if (!ToProp.IsEmpty())
				{
					EMaterialProperty MatProp = MP_BaseColor;
					if (ToProp == TEXT("BaseColor")) MatProp = MP_BaseColor;
					else if (ToProp == TEXT("Metallic")) MatProp = MP_Metallic;
					else if (ToProp == TEXT("Specular")) MatProp = MP_Specular;
					else if (ToProp == TEXT("Roughness")) MatProp = MP_Roughness;
					else if (ToProp == TEXT("EmissiveColor") || ToProp == TEXT("Emissive")) MatProp = MP_EmissiveColor;
					else if (ToProp == TEXT("Normal")) MatProp = MP_Normal;
					else if (ToProp == TEXT("Opacity")) MatProp = MP_Opacity;
					else if (ToProp == TEXT("OpacityMask")) MatProp = MP_OpacityMask;

					UMaterialEditingLibrary::ConnectMaterialProperty(*FromExprPtr, FromOutput, MatProp);
				}
				else if (!ToName.IsEmpty())
				{
					UMaterialExpression** ToExprPtr = CreatedExpressions.Find(ToName);
					if (ToExprPtr && IsValid(*ToExprPtr))
					{
						UMaterialEditingLibrary::ConnectMaterialExpressions(*FromExprPtr, FromOutput, *ToExprPtr, ToInput);
					}
				}
			}
		}
	}

	UMaterialEditingLibrary::RecompileMaterial(NewMaterial);

	UPackage* Package = NewMaterial->GetOutermost();
	if (IsValid(Package))
	{
		Package->MarkPackageDirty();
		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, NewMaterial, *PackageFilename, SaveArgs);
		}
	}
	FAssetRegistryModule::AssetCreated(NewMaterial);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created Material '%s'"), *AssetName);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMaterialActions::ExecuteCreateMaterialInstance(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	FString ParentPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("parent_material"), ParentPath, Result.Errors, true))
	{
		return Result;
	}

	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/Materials/MI_Rock_Wet."),
			*AssetPath));
		return Result;
	}

	UMaterial* ParentMaterial = LoadObject<UMaterial>(nullptr, *ParentPath);
	if (!IsValid(ParentMaterial))
	{
		Result.Errors.Add(FString::Printf(TEXT("Parent material not found or invalid: %s"), *ParentPath));
		return Result;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	if (!IsValid(Factory))
	{
		Result.Errors.Add(TEXT("Failed to create MaterialInstanceConstantFactoryNew instance."));
		return Result;
	}
	Factory->InitialParent = ParentMaterial;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
	UMaterialInstanceConstant* NewMI = Cast<UMaterialInstanceConstant>(NewAsset);

	if (!IsValid(NewMI))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Material Instance at %s"), *AssetPath));
		return Result;
	}

	// CRITICAL: Modify() before changing parameters for undo support
	NewMI->Modify();

	// Set scalar parameters
	const TSharedPtr<FJsonObject>* ScalarsObj = nullptr;
	if (UAgentFrameworkActionUtils::TryGetObjectParam(Params, TEXT("scalar_parameters"), ScalarsObj, Result.Errors, false) && ScalarsObj && (*ScalarsObj).IsValid())
	{
		for (const auto& Pair : (*ScalarsObj)->Values)
		{
			if (Pair.Value.IsValid())
			{
				double Value = 0.0;
				if (Pair.Value->TryGetNumber(Value))
				{
					UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(NewMI, FName(*Pair.Key), static_cast<float>(Value));
				}
			}
		}
	}

	// Set vector parameters
	const TSharedPtr<FJsonObject>* VectorsObj = nullptr;
	if (UAgentFrameworkActionUtils::TryGetObjectParam(Params, TEXT("vector_parameters"), VectorsObj, Result.Errors, false) && VectorsObj && (*VectorsObj).IsValid())
	{
		for (const auto& Pair : (*VectorsObj)->Values)
		{
			if (!Pair.Value.IsValid()) continue;
			const TSharedPtr<FJsonObject>* VecObj = nullptr;
			if (Pair.Value->TryGetObject(VecObj) && VecObj && (*VecObj).IsValid())
			{
				float R = 0.0f, G = 0.0f, B = 0.0f, A = 1.0f;
				UAgentFrameworkActionUtils::TryGetFloatParam(*VecObj, TEXT("r"), R, Result.Errors, false);
				UAgentFrameworkActionUtils::TryGetFloatParam(*VecObj, TEXT("g"), G, Result.Errors, false);
				UAgentFrameworkActionUtils::TryGetFloatParam(*VecObj, TEXT("b"), B, Result.Errors, false);
				UAgentFrameworkActionUtils::TryGetFloatParam(*VecObj, TEXT("a"), A, Result.Errors, false);

				FLinearColor Color(R, G, B, A);
				UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(NewMI, FName(*Pair.Key), Color);
			}
		}
	}

	UPackage* Package = NewMI->GetOutermost();
	if (IsValid(Package))
	{
		Package->MarkPackageDirty();
	}
	FAssetRegistryModule::AssetCreated(NewMI);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created Material Instance '%s' from '%s'"), *AssetName, *ParentPath);
	Result.ModifiedAssets.Add(AssetPath);
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMaterialActions::ExecuteAddMaterialExpression(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	Result.Errors.Add(TEXT("Use create_material with expressions array instead."));
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMaterialActions::ExecuteConnectMaterialProperty(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	Result.Errors.Add(TEXT("Use create_material with expressions array and connections instead."));
	return Result;
}

FAgentFrameworkActionResult FAgentFrameworkMaterialActions::ExecuteCaptureMaterial(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	FString AssetPath;
	if (!UAgentFrameworkActionUtils::TryGetStringParam(Params, TEXT("asset_path"), AssetPath, Result.Errors, true))
	{
		return Result;
	}

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *AssetPath);
	if (!IsValid(Material))
	{
		Result.Errors.Add(FString::Printf(TEXT("Material not found at %s"), *AssetPath));
		return Result;
	}

	UWorld* World = IsValid(GEditor) ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!IsValid(World))
	{
		Result.Errors.Add(TEXT("Could not find a valid World context to capture the material."));
		return Result;
	}

	int32 MaxDimension = 512;
	UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("max_dimension"), MaxDimension, Result.Errors, false);

	int32 Quality = 75;
	UAgentFrameworkActionUtils::TryGetIntParam(Params, TEXT("quality"), Quality, Result.Errors, false);

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
	if (!IsValid(RenderTarget))
	{
		Result.Errors.Add(TEXT("Failed to create UTextureRenderTarget2D instance."));
		return Result;
	}
	RenderTarget->InitCustomFormat(MaxDimension, MaxDimension, PF_B8G8R8A8, false);
	RenderTarget->ClearColor = FLinearColor(0.12f, 0.12f, 0.12f, 1.0f); // neutral dark gray

	UKismetRenderingLibrary::DrawMaterialToRenderTarget(World, RenderTarget, Material);

	FlushRenderingCommands();

	FTextureRenderTargetResource* RTResource = RenderTarget->GameThread_GetRenderTargetResource();
	if (!RTResource)
	{
		Result.Errors.Add(TEXT("Failed to get render target resource."));
		return Result;
	}

	TArray<FColor> Pixels;
	if (!RTResource->ReadPixels(Pixels))
	{
		Result.Errors.Add(TEXT("Failed to read pixels from render target."));
		return Result;
	}

	FString FilePath = FAgentFrameworkViewportActions::SavePixelsToDisk(Pixels, MaxDimension, MaxDimension, MaxDimension, Quality);
	if (FilePath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Failed to save material capture to disk."));
		return Result;
	}

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(
		TEXT("[IMAGE:%s]\n\n"
			 "Material '%s' captured successfully (rendered at %dx%d, resized to max %dpx, JPEG quality %d). "
			 "The image has been saved to the path above."),
		*FilePath, *AssetPath, MaxDimension, MaxDimension, MaxDimension, Quality);

	return Result;
}

void FAgentFrameworkMaterialActions::PlaySuccessSound()
{
#if WITH_EDITOR
	if (IsValid(GEditor))
	{
		USoundBase* SuccessSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileSuccess.CompileSuccess"));
		if (IsValid(SuccessSound))
		{
			GEditor->PlayEditorSound(SuccessSound);
		}
	}
#endif
}

FAgentFrameworkActionResult FAgentFrameworkMaterialActions::ExecuteCreatePBRMaterialFromTextures(const TSharedRef<FJsonObject>& Params, FAgentFrameworkActionResult& Result)
{
	// 1. Resolve asset path
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("material_path"), AssetPath) || AssetPath.IsEmpty())
	{
		if (!Params->TryGetStringField(TEXT("MaterialPath"), AssetPath) || AssetPath.IsEmpty())
		{
			if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
			{
				if (!Params->TryGetStringField(TEXT("AssetPath"), AssetPath) || AssetPath.IsEmpty())
				{
					FString DestPath, MatName;
					bool bHasDest = Params->TryGetStringField(TEXT("destination_path"), DestPath) || Params->TryGetStringField(TEXT("DestinationPath"), DestPath);
					bool bHasName = Params->TryGetStringField(TEXT("material_name"), MatName) || Params->TryGetStringField(TEXT("MaterialName"), MatName);
					if (bHasDest && bHasName)
					{
						DestPath.RemoveFromEnd(TEXT("/"));
						AssetPath = FString::Printf(TEXT("%s/%s"), *DestPath, *MatName);
					}
					else if (bHasDest && !DestPath.IsEmpty())
					{
						AssetPath = DestPath;
					}
				}
			}
		}
	}

	if (AssetPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: material_path (or asset_path / destination_path)."));
		return Result;
	}

	// 2. Parse BlendMode
	EBlendMode BlendMode = BLEND_Opaque;
	FString BlendModeStr;
	if (Params->TryGetStringField(TEXT("blend_mode"), BlendModeStr) || Params->TryGetStringField(TEXT("BlendMode"), BlendModeStr))
	{
		if (BlendModeStr.Equals(TEXT("Masked"), ESearchCase::IgnoreCase)) BlendMode = BLEND_Masked;
		else if (BlendModeStr.Equals(TEXT("Translucent"), ESearchCase::IgnoreCase)) BlendMode = BLEND_Translucent;
		else if (BlendModeStr.Equals(TEXT("Additive"), ESearchCase::IgnoreCase)) BlendMode = BLEND_Additive;
		else if (BlendModeStr.Equals(TEXT("Modulate"), ESearchCase::IgnoreCase)) BlendMode = BLEND_Modulate;
		else if (BlendModeStr.Equals(TEXT("AlphaComposite"), ESearchCase::IgnoreCase)) BlendMode = BLEND_AlphaComposite;
		else if (BlendModeStr.Equals(TEXT("AlphaHoldout"), ESearchCase::IgnoreCase)) BlendMode = BLEND_AlphaHoldout;
		else BlendMode = BLEND_Opaque;
	}

	// 3. Parse ShadingModel
	EMaterialShadingModel ShadingModel = MSM_DefaultLit;
	FString ShadingModelStr;
	if (Params->TryGetStringField(TEXT("shading_model"), ShadingModelStr) || Params->TryGetStringField(TEXT("ShadingModel"), ShadingModelStr))
	{
		if (ShadingModelStr.Equals(TEXT("Unlit"), ESearchCase::IgnoreCase)) ShadingModel = MSM_Unlit;
		else if (ShadingModelStr.Equals(TEXT("Subsurface"), ESearchCase::IgnoreCase)) ShadingModel = MSM_Subsurface;
		else if (ShadingModelStr.Equals(TEXT("SubsurfaceProfile"), ESearchCase::IgnoreCase)) ShadingModel = MSM_SubsurfaceProfile;
		else if (ShadingModelStr.Equals(TEXT("ClearCoat"), ESearchCase::IgnoreCase)) ShadingModel = MSM_ClearCoat;
		else if (ShadingModelStr.Equals(TEXT("TwoSidedFoliage"), ESearchCase::IgnoreCase)) ShadingModel = MSM_TwoSidedFoliage;
		else if (ShadingModelStr.Equals(TEXT("Hair"), ESearchCase::IgnoreCase)) ShadingModel = MSM_Hair;
		else if (ShadingModelStr.Equals(TEXT("Cloth"), ESearchCase::IgnoreCase)) ShadingModel = MSM_Cloth;
		else if (ShadingModelStr.Equals(TEXT("Eye"), ESearchCase::IgnoreCase)) ShadingModel = MSM_Eye;
		else if (ShadingModelStr.Equals(TEXT("SingleLayerWater"), ESearchCase::IgnoreCase)) ShadingModel = MSM_SingleLayerWater;
		else ShadingModel = MSM_DefaultLit;
	}

	// 4. Parse TwoSided
	bool bTwoSided = false;
	if (!Params->TryGetBoolField(TEXT("two_sided"), bTwoSided))
	{
		if (!Params->TryGetBoolField(TEXT("TwoSided"), bTwoSided))
		{
			Params->TryGetBoolField(TEXT("bTwoSided"), bTwoSided);
		}
	}

	// 5. Helper lambda to retrieve texture paths from top level or texture_maps object
	auto GetTexturePath = [&](const TArray<FString>& CandidateKeys) -> FString
	{
		for (const FString& Key : CandidateKeys)
		{
			FString Val;
			if (Params->TryGetStringField(Key, Val) && !Val.IsEmpty())
			{
				return Val;
			}
		}
		const TSharedPtr<FJsonObject>* TextureMapsObj = nullptr;
		if ((Params->TryGetObjectField(TEXT("texture_maps"), TextureMapsObj) || Params->TryGetObjectField(TEXT("TextureMaps"), TextureMapsObj)) && TextureMapsObj && (*TextureMapsObj).IsValid())
		{
			for (const FString& Key : CandidateKeys)
			{
				FString Val;
				if ((*TextureMapsObj)->TryGetStringField(Key, Val) && !Val.IsEmpty())
				{
					return Val;
				}
			}
		}
		return FString();
	};

	FString BaseColorPath = GetTexturePath({ TEXT("base_color_texture_path"), TEXT("BaseColorTexturePath"), TEXT("base_color"), TEXT("BaseColor"), TEXT("base_color_path"), TEXT("BaseColorPath"), TEXT("BC") });
	FString NormalPath = GetTexturePath({ TEXT("normal_texture_path"), TEXT("NormalTexturePath"), TEXT("normal"), TEXT("Normal"), TEXT("normal_path"), TEXT("NormalPath"), TEXT("normal_map_texture_path"), TEXT("N") });
	FString RoughnessPath = GetTexturePath({ TEXT("roughness_texture_path"), TEXT("RoughnessTexturePath"), TEXT("roughness"), TEXT("Roughness"), TEXT("roughness_path"), TEXT("RoughnessPath"), TEXT("R") });
	FString MetallicPath = GetTexturePath({ TEXT("metallic_texture_path"), TEXT("MetallicTexturePath"), TEXT("metallic"), TEXT("Metallic"), TEXT("metallic_path"), TEXT("MetallicPath"), TEXT("M") });
	FString AOPath = GetTexturePath({ TEXT("ao_texture_path"), TEXT("AOTexturePath"), TEXT("ao"), TEXT("AO"), TEXT("ambient_occlusion_texture_path"), TEXT("AmbientOcclusionTexturePath"), TEXT("ambient_occlusion"), TEXT("AmbientOcclusion") });
	FString SpecularPath = GetTexturePath({ TEXT("specular_texture_path"), TEXT("SpecularTexturePath"), TEXT("specular"), TEXT("Specular"), TEXT("specular_path"), TEXT("SpecularPath"), TEXT("S") });
	FString EmissivePath = GetTexturePath({ TEXT("emissive_texture_path"), TEXT("EmissiveTexturePath"), TEXT("emissive"), TEXT("Emissive"), TEXT("emissive_path"), TEXT("EmissivePath"), TEXT("E") });
	FString OpacityPath = GetTexturePath({ TEXT("opacity_texture_path"), TEXT("OpacityTexturePath"), TEXT("opacity"), TEXT("Opacity"), TEXT("opacity_path"), TEXT("OpacityPath"), TEXT("O") });

	if (BaseColorPath.IsEmpty())
	{
		Result.Errors.Add(TEXT("Missing required parameter: base_color_texture_path (or base_color)."));
		return Result;
	}

	// 6. Create Material Asset
	FString PackageName, PackagePath, AssetName;
	UAgentFrameworkActionUtils::SplitAssetPath(AssetPath, PackageName, PackagePath, AssetName);

	if (AssetName.IsEmpty() || PackagePath.IsEmpty())
	{
		Result.Errors.Add(FString::Printf(
			TEXT("asset_path '%s' does not name an asset. Provide a full path including the asset name, e.g. /Game/Materials/M_Rock."),
			*AssetPath));
		return Result;
	}

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	IAssetTools& AssetTools = AssetToolsModule.Get();

	// Load through the explicit object path — a bare package path does not reliably resolve.
	UMaterial* NewMaterial = LoadObject<UMaterial>(nullptr, *FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName));
	if (!IsValid(NewMaterial))
	{
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
		if (!IsValid(Factory))
		{
			Result.Errors.Add(TEXT("Failed to create MaterialFactoryNew instance."));
			return Result;
		}

		UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);
		NewMaterial = Cast<UMaterial>(NewAsset);
	}

	if (!IsValid(NewMaterial))
	{
		Result.Errors.Add(FString::Printf(TEXT("Failed to create Material asset at %s"), *AssetPath));
		return Result;
	}

	NewMaterial->Modify();
	NewMaterial->BlendMode = BlendMode;
	NewMaterial->SetShadingModel(ShadingModel);
	NewMaterial->TwoSided = bTwoSided;

	// 7. Define Texture Slots
	struct FPBRSlotDef
	{
		FString SlotName;
		FString TexturePath;
		EMaterialSamplerType SamplerType;
		EMaterialProperty MatProperty;
		bool bRequired;
	};

	TArray<FPBRSlotDef> SlotsToProcess;
	SlotsToProcess.Add({ TEXT("BaseColorMap"), BaseColorPath, SAMPLERTYPE_Color, MP_BaseColor, true });

	if (!NormalPath.IsEmpty())
	{
		SlotsToProcess.Add({ TEXT("NormalMap"), NormalPath, SAMPLERTYPE_Normal, MP_Normal, false });
	}
	if (!RoughnessPath.IsEmpty())
	{
		SlotsToProcess.Add({ TEXT("RoughnessMap"), RoughnessPath, SAMPLERTYPE_LinearColor, MP_Roughness, false });
	}
	if (!MetallicPath.IsEmpty())
	{
		SlotsToProcess.Add({ TEXT("MetallicMap"), MetallicPath, SAMPLERTYPE_LinearColor, MP_Metallic, false });
	}
	if (!AOPath.IsEmpty())
	{
		SlotsToProcess.Add({ TEXT("AOMap"), AOPath, SAMPLERTYPE_LinearColor, MP_AmbientOcclusion, false });
	}
	if (!SpecularPath.IsEmpty())
	{
		SlotsToProcess.Add({ TEXT("SpecularMap"), SpecularPath, SAMPLERTYPE_LinearColor, MP_Specular, false });
	}
	if (!EmissivePath.IsEmpty())
	{
		SlotsToProcess.Add({ TEXT("EmissiveMap"), EmissivePath, SAMPLERTYPE_Color, MP_EmissiveColor, false });
	}
	if (!OpacityPath.IsEmpty())
	{
		EMaterialProperty OpacityProp = (BlendMode == BLEND_Masked) ? MP_OpacityMask : MP_Opacity;
		SlotsToProcess.Add({ TEXT("OpacityMap"), OpacityPath, SAMPLERTYPE_LinearColor, OpacityProp, false });
	}

	// 8. Instantiate expressions and connect pins
	int32 PosX = -400;
	int32 PosY = 0;
	int32 TexturesAssignedCount = 0;
	TArray<FString> AssignedSlots;

	for (const FPBRSlotDef& Slot : SlotsToProcess)
	{
		UTexture2D* TextureAsset = LoadObject<UTexture2D>(nullptr, *Slot.TexturePath);
		if (!IsValid(TextureAsset))
		{
			if (Slot.bRequired)
			{
				Result.Errors.Add(FString::Printf(TEXT("Failed to load required BaseColor texture asset at '%s'"), *Slot.TexturePath));
				return Result;
			}
			else
			{
				Result.Warnings.Add(FString::Printf(TEXT("Failed to load optional texture asset at '%s' for slot '%s'"), *Slot.TexturePath, *Slot.SlotName));
				continue;
			}
		}

		UMaterialExpressionTextureSampleParameter2D* TexExpr = Cast<UMaterialExpressionTextureSampleParameter2D>(
			UMaterialEditingLibrary::CreateMaterialExpression(NewMaterial, UMaterialExpressionTextureSampleParameter2D::StaticClass(), PosX, PosY));

		if (!IsValid(TexExpr))
		{
			Result.Warnings.Add(FString::Printf(TEXT("Failed to create material expression for slot '%s'"), *Slot.SlotName));
			continue;
		}

		TexExpr->ParameterName = FName(*Slot.SlotName);
		TexExpr->SamplerType = Slot.SamplerType;
		TexExpr->Texture = TextureAsset;

		UMaterialEditingLibrary::ConnectMaterialProperty(TexExpr, TEXT(""), Slot.MatProperty);

		PosY += 220;
		TexturesAssignedCount++;
		AssignedSlots.Add(Slot.SlotName);
	}

	// 9. Recompile, Dirty & Save Package
	UMaterialEditingLibrary::RecompileMaterial(NewMaterial);

	UPackage* Package = NewMaterial->GetOutermost();
	if (IsValid(Package))
	{
		Package->MarkPackageDirty();
		FString PackageFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			UPackage::SavePackage(Package, NewMaterial, *PackageFilename, SaveArgs);
		}
	}
	FAssetRegistryModule::AssetCreated(NewMaterial);

	Result.bSuccess = true;
	Result.ResultMessage = FString::Printf(TEXT("Created PBR Material '%s' with %d texture parameters connected (%s)."),
		*AssetPath, TexturesAssignedCount, *FString::Join(AssignedSlots, TEXT(", ")));
	Result.ModifiedAssets.Add(AssetPath);

	UE_LOG(LogAgentFramework, Log, TEXT("MaterialActions: Created PBR Material '%s' with %d textures."), *AssetPath, TexturesAssignedCount);

	return Result;
}

