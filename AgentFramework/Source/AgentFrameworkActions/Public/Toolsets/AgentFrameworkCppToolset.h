// Copyright 2026 AgentFramework. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Optional.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "AgentFrameworkCppToolset.generated.h"

class UAsyncFunctionResultString;

USTRUCT(BlueprintType)
struct FAgentFrameworkCppPropertySpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Type = TEXT("float");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString DefaultValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Category = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	bool bEditAnywhere = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	bool bBlueprintReadWrite = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	bool bReplicated = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Comment;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkCppMethodSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString ReturnType = TEXT("void");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Parameters;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Body;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Category = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	bool bBlueprintCallable = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	bool bConst = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	bool bOverride = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentFramework|Cpp")
	FString Comment;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkCppClassResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString ClassName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString PrefixedClassName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString ParentClassName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString ModuleName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString HeaderPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString SourcePath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	bool bRequiresRebuild = true;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkCppReflectedProperty
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString Category;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	bool bEditAnywhere = false;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	bool bBlueprintReadWrite = false;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	bool bBlueprintReadOnly = false;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	bool bReplicated = false;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	TMap<FString, FString> MetaData;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkCppReflectedParam
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	bool bOutParam = false;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	bool bReturnParam = false;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkCppReflectedFunction
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString ReturnType = TEXT("void");

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	TArray<FString> Flags;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	TArray<FAgentFrameworkCppReflectedParam> Parameters;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkCppReflectedEnumValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	int64 Value = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString DisplayName;
};

USTRUCT(BlueprintType)
struct FAgentFrameworkCppReflectionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString TypeName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString FullPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString TypeKind;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString ParentClassName;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString ModulePackage;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	FString HeaderPath;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	TArray<FString> Flags;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	TArray<FString> Interfaces;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	TArray<FAgentFrameworkCppReflectedProperty> Properties;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	TArray<FAgentFrameworkCppReflectedFunction> Functions;

	UPROPERTY(BlueprintReadOnly, Category = "AgentFramework|Cpp")
	TArray<FAgentFrameworkCppReflectedEnumValue> EnumValues;
};

UCLASS(BlueprintType)
class AGENTFRAMEWORKACTIONS_API UAgentFrameworkCppToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Cpp")
	static FAgentFrameworkCppClassResult CreateCppClass(
		const FString& ClassName,
		const FString& ParentClassName = TEXT("Actor"),
		const FString& ModuleName = TEXT(""),
		const FString& SubDirectory = TEXT(""));

	UFUNCTION(meta = (AICallable, AIAccessMode = "Write"), Category = "AgentFramework|Cpp")
	static FAgentFrameworkCppClassResult ScaffoldCppClass(
		const FString& ClassName,
		const TOptional<FString>& ParentClassName,
		const TOptional<FString>& ModuleName,
		const TOptional<FString>& SubDirectory,
		const TOptional<TArray<FAgentFrameworkCppPropertySpec>>& Properties,
		const TOptional<TArray<FAgentFrameworkCppMethodSpec>>& Methods,
		const TOptional<TArray<FString>>& AdditionalIncludes);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Execute"), Category = "AgentFramework|Cpp")
	static UAsyncFunctionResultString* RegenerateProjectFiles(int32 TimeoutSeconds = 180);

	UFUNCTION(meta = (AICallable, AIAccessMode = "Read"), Category = "AgentFramework|Cpp")
	static FAgentFrameworkCppReflectionInfo GetCppReflectionInfo(
		const FString& TypeName,
		bool bIncludeInherited = false);

	static bool IsWritableSourcePath(const FString& FilePath);
	static bool ValidateCodeSafety(const FString& CodeSnippet, FString& OutRejectionReason);
	static bool ValidateCodeSafety(const FString& CodeSnippet);
};
