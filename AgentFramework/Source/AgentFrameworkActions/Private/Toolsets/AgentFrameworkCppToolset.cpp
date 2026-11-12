// Copyright 2026 AgentFramework. All Rights Reserved.

#include "Toolsets/AgentFrameworkCppToolset.h"
#include "AgentFrameworkActionUtils.h"
#include "AsyncFunctionResult/AsyncFunctionResultString.h"
#include "AsyncFunctionResult/AsyncFunctionResultUtils.h"
#include "HAL/FileManager.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "UObject/UObjectIterator.h"

namespace AgentFrameworkCppToolsetInternal
{
	static FString NormalizeHeaderLine(const FString& RawHeader)
	{
		FString Trimmed = RawHeader.TrimStartAndEnd();
		Trimmed.ReplaceInline(TEXT("\\"), TEXT("/"));
		Trimmed.RemoveFromStart(TEXT("\""));
		Trimmed.RemoveFromEnd(TEXT("\""));
		return Trimmed;
	}

	static FString SanitizeSingleLineComment(const FString& RawComment)
	{
		FString Clean = RawComment;
		Clean.ReplaceInline(TEXT("\r"), TEXT(" "));
		Clean.ReplaceInline(TEXT("\n"), TEXT(" "));
		Clean.ReplaceInline(TEXT("*/"), TEXT("* /"));
		return Clean.TrimStartAndEnd();
	}

	static FString SanitizeCategoryString(const FString& RawCategory)
	{
		FString Clean = RawCategory;
		Clean.ReplaceInline(TEXT("\r"), TEXT(" "));
		Clean.ReplaceInline(TEXT("\n"), TEXT(" "));
		Clean.ReplaceInline(TEXT("\\"), TEXT(""));
		Clean.ReplaceInline(TEXT("\""), TEXT(""));
		Clean = Clean.TrimStartAndEnd();
		return Clean.IsEmpty() ? TEXT("Default") : Clean;
	}

	static bool ContainsControlOrNewline(const FString& Value)
	{
		for (TCHAR Ch : Value)
		{
			if (Ch == TEXT('\r') || Ch == TEXT('\n') || Ch == TEXT('\0'))
			{
				return true;
			}
		}
		return false;
	}

	static bool IsStaleOrInternalClass(const UClass* Class)
	{
		if (!Class || Class->HasAnyClassFlags(CLASS_NewerVersionExists))
		{
			return true;
		}
		const FString Name = Class->GetName();
		return Name.StartsWith(TEXT("SKEL_"))
			|| Name.StartsWith(TEXT("TRASHCLASS_"))
			|| Name.StartsWith(TEXT("REINST_"))
			|| Name.StartsWith(TEXT("HOTRELOADED_"));
	}

	static FString ExtractIncludePathFromDiskHeader(const FString& DiskHeaderPath)
	{
		if (DiskHeaderPath.IsEmpty())
		{
			return TEXT("");
		}
		FString Normalized = DiskHeaderPath;
		Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
		int32 MarkerIdx = Normalized.Find(TEXT("/Public/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (MarkerIdx != INDEX_NONE)
		{
			return Normalized.RightChop(MarkerIdx + 8);
		}
		MarkerIdx = Normalized.Find(TEXT("/Classes/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (MarkerIdx != INDEX_NONE)
		{
			return Normalized.RightChop(MarkerIdx + 9);
		}
		return FPaths::GetCleanFilename(Normalized);
	}

	static bool ResolveParentUClass(
		const FString& RawParentName,
		UClass*& OutParentClass,
		FString& OutPrefixedParent,
		FString& OutParentInclude,
		FString& OutChildPrefix,
		FString& OutError)
	{
		OutParentClass = nullptr;
		const FString Trimmed = RawParentName.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutError = TEXT("ParentClassName cannot be empty.");
			return false;
		}

		TArray<UClass*> ExactMatches;
		TArray<UClass*> PrefixedMatches;

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (IsStaleOrInternalClass(Candidate))
			{
				continue;
			}
			const FString CandidateName = Candidate->GetName();
			if (Candidate->GetPathName().Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				ExactMatches = { Candidate };
				break;
			}
			if (CandidateName.Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				ExactMatches.Add(Candidate);
			}
			else if (CandidateName.Equals(TEXT("A") + Trimmed, ESearchCase::IgnoreCase)
				|| CandidateName.Equals(TEXT("U") + Trimmed, ESearchCase::IgnoreCase))
			{
				PrefixedMatches.Add(Candidate);
			}
		}

		const TArray<UClass*>& Chosen = ExactMatches.Num() > 0 ? ExactMatches : PrefixedMatches;
		if (Chosen.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Parent class '%s' was not found among loaded reflection types."), *Trimmed);
			return false;
		}
		if (Chosen.Num() > 1)
		{
			TArray<FString> Paths;
			for (UClass* Match : Chosen)
			{
				Paths.Add(Match->GetPathName());
			}
			OutError = FString::Printf(TEXT("Ambiguous parent class '%s'. Specify a full class path from: %s"), *Trimmed, *FString::Join(Paths, TEXT(", ")));
			return false;
		}

		OutParentClass = Chosen[0];
		OutChildPrefix = OutParentClass->IsChildOf<AActor>() ? TEXT("A") : TEXT("U");
		OutPrefixedParent = FString::Printf(TEXT("%s%s"), OutParentClass->GetPrefixCPP(), *OutParentClass->GetName());

		if (OutParentClass == UObject::StaticClass())
		{
			OutParentInclude.Empty();
		}
		else if (OutParentClass == AActor::StaticClass())
		{
			OutParentInclude = TEXT("GameFramework/Actor.h");
		}
		else if (OutParentClass == UActorComponent::StaticClass())
		{
			OutParentInclude = TEXT("Components/ActorComponent.h");
		}
		else if (OutParentClass == USceneComponent::StaticClass())
		{
			OutParentInclude = TEXT("Components/SceneComponent.h");
		}
		else
		{
			FString ResolvedHeader;
			if (FSourceCodeNavigation::FindClassHeaderPath(OutParentClass, ResolvedHeader))
			{
				OutParentInclude = ExtractIncludePathFromDiskHeader(ResolvedHeader);
			}
			else if (OutParentClass->HasMetaData(TEXT("IncludePath")))
			{
				OutParentInclude = OutParentClass->GetMetaData(TEXT("IncludePath"));
			}
		}

		return true;
	}

	static void DeriveClassNames(
		const FString& RawClassName,
		const FString& ExpectedPrefix,
		bool bIsInterface,
		FString& OutBaseFileName,
		FString& OutPrefixedClassName)
	{
		OutBaseFileName = RawClassName;
		if (RawClassName.Len() > 1 && FChar::IsUpper(RawClassName[1]))
		{
			const FString FirstChar = RawClassName.Left(1);
			if (FirstChar.Equals(ExpectedPrefix, ESearchCase::CaseSensitive)
				|| (bIsInterface && FirstChar.Equals(TEXT("I"), ESearchCase::CaseSensitive)))
			{
				OutBaseFileName = RawClassName.RightChop(1);
				OutPrefixedClassName = ExpectedPrefix + OutBaseFileName;
				return;
			}
		}
		OutPrefixedClassName = ExpectedPrefix + RawClassName;
	}

	static bool CheckBalancedBraces(const FString& Body, FString& OutError)
	{
		int32 Depth = 0;
		bool bInLineComment = false;
		bool bInBlockComment = false;
		bool bInString = false;
		bool bInChar = false;
		bool bEscaped = false;

		for (int32 i = 0; i < Body.Len(); ++i)
		{
			const TCHAR Ch = Body[i];
			const TCHAR Next = (i + 1 < Body.Len()) ? Body[i + 1] : TEXT('\0');

			if (bInLineComment)
			{
				if (Ch == TEXT('\n'))
				{
					bInLineComment = false;
				}
				continue;
			}
			if (bInBlockComment)
			{
				if (Ch == TEXT('*') && Next == TEXT('/'))
				{
					bInBlockComment = false;
					++i;
				}
				continue;
			}
			if (bInString)
			{
				if (bEscaped) { bEscaped = false; continue; }
				if (Ch == TEXT('\\')) { bEscaped = true; continue; }
				if (Ch == TEXT('"')) { bInString = false; }
				continue;
			}
			if (bInChar)
			{
				if (bEscaped) { bEscaped = false; continue; }
				if (Ch == TEXT('\\')) { bEscaped = true; continue; }
				if (Ch == TEXT('\'')) { bInChar = false; }
				continue;
			}

			if (Ch == TEXT('/') && Next == TEXT('/')) { bInLineComment = true; ++i; continue; }
			if (Ch == TEXT('/') && Next == TEXT('*')) { bInBlockComment = true; ++i; continue; }
			if (Ch == TEXT('"')) { bInString = true; continue; }
			if (Ch == TEXT('\'')) { bInChar = true; continue; }

			if (Ch == TEXT('{'))
			{
				++Depth;
			}
			else if (Ch == TEXT('}'))
			{
				--Depth;
				if (Depth < 0)
				{
					OutError = TEXT("Method body contains unmatched closing brace '}'.");
					return false;
				}
			}
		}

		if (bInString || bInChar || bInBlockComment)
		{
			OutError = TEXT("Method body contains unterminated string literal or block comment.");
			return false;
		}
		if (Depth != 0)
		{
			OutError = TEXT("Method body contains unbalanced curly braces '{' / '}'.");
			return false;
		}
		return true;
	}
}

bool UAgentFrameworkCppToolset::IsWritableSourcePath(const FString& FilePath)
{
	return UAgentFrameworkActionUtils::IsWritableSourcePath(FilePath);
}

bool UAgentFrameworkCppToolset::ValidateCodeSafety(const FString& CodeSnippet, FString& OutRejectionReason)
{
	OutRejectionReason.Reset();
	if (CodeSnippet.IsEmpty())
	{
		return true;
	}

	static const TCHAR* ForbiddenPatterns[] = {
		TEXT("system("),
		TEXT("_wsystem("),
		TEXT("popen("),
		TEXT("_popen("),
		TEXT("WinExec"),
		TEXT("ShellExecute"),
		TEXT("CreateProcess"),
		TEXT("FPlatformProcess::CreateProc"),
		TEXT("FPlatformProcess::ExecProcess"),
		TEXT("FPlatformProcess::LaunchFile"),
		TEXT("FFileHelper::Save"),
		TEXT("IFileManager::Get().Delete"),
		TEXT("std::ofstream"),
		TEXT("std::fstream"),
		TEXT("fopen("),
		TEXT("freopen("),
		TEXT("LoadLibrary"),
		TEXT("GetProcAddress"),
		TEXT("VirtualAlloc"),
		TEXT("VirtualProtect"),
		TEXT("__asm"),
		TEXT("asm("),
		TEXT("asm volatile"),
		TEXT("#import")
	};

	for (const TCHAR* Pattern : ForbiddenPatterns)
	{
		if (CodeSnippet.Contains(Pattern, ESearchCase::CaseSensitive))
		{
			OutRejectionReason = FString::Printf(TEXT("Disallowed code pattern '%s' detected."), Pattern);
			return false;
		}
	}

	if (CodeSnippet.Contains(TEXT("WinExec"), ESearchCase::IgnoreCase)
		|| CodeSnippet.Contains(TEXT("ShellExecute"), ESearchCase::IgnoreCase)
		|| CodeSnippet.Contains(TEXT("CreateProcess"), ESearchCase::IgnoreCase))
	{
		OutRejectionReason = TEXT("Disallowed OS process execution API detected.");
		return false;
	}

	return true;
}

bool UAgentFrameworkCppToolset::ValidateCodeSafety(const FString& CodeSnippet)
{
	FString IgnoredReason;
	return ValidateCodeSafety(CodeSnippet, IgnoredReason);
}

FAgentFrameworkCppClassResult UAgentFrameworkCppToolset::CreateCppClass(
	const FString& ClassName,
	const FString& ParentClassName,
	const FString& ModuleName,
	const FString& SubDirectory)
{
	FAgentFrameworkCppClassResult Result;
	const FString TrimmedClassName = ClassName.TrimStartAndEnd();
	if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(TrimmedClassName))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateCppClass"), FString::Printf(TEXT("Invalid C++ class name '%s'."), *ClassName));
		return Result;
	}

	UClass* ParentClass = nullptr;
	FString PrefixedParent;
	FString ParentInclude;
	FString ChildPrefix;
	FString Error;
	if (!AgentFrameworkCppToolsetInternal::ResolveParentUClass(ParentClassName, ParentClass, PrefixedParent, ParentInclude, ChildPrefix, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateCppClass"), Error);
		return Result;
	}

	const bool bIsInterface = ParentClass && ParentClass->IsChildOf<UInterface>();
	FString BaseFileName;
	FString PrefixedClassName;
	AgentFrameworkCppToolsetInternal::DeriveClassNames(TrimmedClassName, ChildPrefix, bIsInterface, BaseFileName, PrefixedClassName);
	if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(BaseFileName) || !UAgentFrameworkActionUtils::IsValidCppIdentifier(PrefixedClassName))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateCppClass"), FString::Printf(TEXT("Invalid derived identifier for '%s'."), *ClassName));
		return Result;
	}

	FString ResolvedModuleName;
	FString HeaderDir;
	FString SourceDir;
	FString IncludePrefix;
	FString ApiMacro;
	if (!UAgentFrameworkActionUtils::ResolveWritableModulePaths(ModuleName, SubDirectory, ResolvedModuleName, HeaderDir, SourceDir, IncludePrefix, ApiMacro, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateCppClass"), Error);
		return Result;
	}

	const FString HeaderPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(HeaderDir, BaseFileName + TEXT(".h")));
	const FString SourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SourceDir, BaseFileName + TEXT(".cpp")));

	if (!IsWritableSourcePath(HeaderPath) || !IsWritableSourcePath(SourcePath))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateCppClass"), FString::Printf(TEXT("Target paths are outside writable project Source/Plugin directories: '%s', '%s'."), *HeaderPath, *SourcePath));
		return Result;
	}

	if (FPaths::FileExists(HeaderPath) || FPaths::FileExists(SourcePath))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateCppClass"), FString::Printf(TEXT("C++ files already exist for '%s' (%s / %s)."), *BaseFileName, *HeaderPath, *SourcePath));
		return Result;
	}

	FString HeaderContent;
	HeaderContent += TEXT("// Copyright AgentFramework. All Rights Reserved.\n\n#pragma once\n\n#include \"CoreMinimal.h\"\n");
	if (!ParentInclude.IsEmpty())
	{
		HeaderContent += FString::Printf(TEXT("#include \"%s\"\n"), *ParentInclude);
	}
	HeaderContent += FString::Printf(TEXT("#include \"%s.generated.h\"\n\n"), *BaseFileName);
	HeaderContent += TEXT("UCLASS()\n");
	HeaderContent += FString::Printf(TEXT("class %s %s : public %s\n{\n\tGENERATED_BODY()\n\npublic:\n\t%s();\n};\n"),
		*ApiMacro, *PrefixedClassName, *PrefixedParent, *PrefixedClassName);

	const FString CppInclude = IncludePrefix + BaseFileName + TEXT(".h");
	FString SourceContent;
	SourceContent += FString::Printf(TEXT("// Copyright AgentFramework. All Rights Reserved.\n\n#include \"%s\"\n\n%s::%s()\n{\n}\n"),
		*CppInclude, *PrefixedClassName, *PrefixedClassName);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(HeaderPath), true);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SourcePath), true);

	if (!FFileHelper::SaveStringToFile(HeaderContent, *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateCppClass"), FString::Printf(TEXT("Failed to write header file '%s'."), *HeaderPath));
		return Result;
	}

	if (!FFileHelper::SaveStringToFile(SourceContent, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*HeaderPath, false, true);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("CreateCppClass"), FString::Printf(TEXT("Failed to write source file '%s'; rolled back header."), *SourcePath));
		return Result;
	}

	Result.ClassName = BaseFileName;
	Result.PrefixedClassName = PrefixedClassName;
	Result.ParentClassName = PrefixedParent;
	Result.ModuleName = ResolvedModuleName;
	Result.HeaderPath = HeaderPath;
	Result.SourcePath = SourcePath;
	Result.bRequiresRebuild = true;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("CreateCppClass"), 0.0, true, {});
	return Result;
}

FAgentFrameworkCppClassResult UAgentFrameworkCppToolset::ScaffoldCppClass(
	const FString& ClassName,
	const TOptional<FString>& ParentClassName,
	const TOptional<FString>& ModuleName,
	const TOptional<FString>& SubDirectory,
	const TOptional<TArray<FAgentFrameworkCppPropertySpec>>& Properties,
	const TOptional<TArray<FAgentFrameworkCppMethodSpec>>& Methods,
	const TOptional<TArray<FString>>& AdditionalIncludes)
{
	FAgentFrameworkCppClassResult Result;
	const FString TrimmedClassName = ClassName.TrimStartAndEnd();
	if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(TrimmedClassName))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Invalid C++ type name '%s'."), *ClassName));
		return Result;
	}

	const FString RequestedParent = (ParentClassName.IsSet() && !ParentClassName->TrimStartAndEnd().IsEmpty())
		? ParentClassName->TrimStartAndEnd()
		: TEXT("Actor");

	const bool bIsStruct = RequestedParent.Equals(TEXT("Struct"), ESearchCase::IgnoreCase)
		|| RequestedParent.Equals(TEXT("UStruct"), ESearchCase::IgnoreCase)
		|| RequestedParent.Equals(TEXT("FStruct"), ESearchCase::IgnoreCase);

	UClass* ParentUClass = nullptr;
	FString PrefixedParent;
	FString ParentInclude;
	FString ChildPrefix;
	FString Error;

	if (bIsStruct)
	{
		ChildPrefix = TEXT("F");
		PrefixedParent = TEXT("");
	}
	else if (!AgentFrameworkCppToolsetInternal::ResolveParentUClass(RequestedParent, ParentUClass, PrefixedParent, ParentInclude, ChildPrefix, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), Error);
		return Result;
	}

	const bool bIsInterface = ParentUClass && ParentUClass->IsChildOf<UInterface>();

	FString BaseFileName;
	FString PrefixedTypeName;
	AgentFrameworkCppToolsetInternal::DeriveClassNames(TrimmedClassName, ChildPrefix, bIsInterface, BaseFileName, PrefixedTypeName);
	if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(BaseFileName) || !UAgentFrameworkActionUtils::IsValidCppIdentifier(PrefixedTypeName))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Invalid derived C++ symbol for '%s'."), *ClassName));
		return Result;
	}

	// Validate AdditionalIncludes
	TArray<FString> CleanIncludes;
	if (!ParentInclude.IsEmpty())
	{
		CleanIncludes.AddUnique(ParentInclude);
	}
	if (AdditionalIncludes.IsSet())
	{
		for (const FString& RawInc : *AdditionalIncludes)
		{
			if (AgentFrameworkCppToolsetInternal::ContainsControlOrNewline(RawInc) || RawInc.Contains(TEXT("..")))
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Unsafe include path '%s'."), *RawInc));
				return Result;
			}
			const FString NormalizedInc = AgentFrameworkCppToolsetInternal::NormalizeHeaderLine(RawInc);
			if (NormalizedInc.IsEmpty() || (!NormalizedInc.EndsWith(TEXT(".h"), ESearchCase::IgnoreCase) && !NormalizedInc.EndsWith(TEXT(".hpp"), ESearchCase::IgnoreCase)))
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Invalid include header '%s': expected '.h' or '.hpp' path."), *RawInc));
				return Result;
			}
			CleanIncludes.AddUnique(NormalizedInc);
		}
	}

	// Validate all properties and methods before writing anything
	bool bHasReplicatedProps = false;
	TSet<FString> SeenPropNames;
	const TArray<FAgentFrameworkCppPropertySpec> PropList = Properties.Get(TArray<FAgentFrameworkCppPropertySpec>{});
	for (const FAgentFrameworkCppPropertySpec& Prop : PropList)
	{
		if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(Prop.Name))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Invalid C++ property identifier '%s'."), *Prop.Name));
			return Result;
		}
		if (SeenPropNames.Contains(Prop.Name))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Duplicate property name '%s'."), *Prop.Name));
			return Result;
		}
		SeenPropNames.Add(Prop.Name);

		if (Prop.Type.TrimStartAndEnd().IsEmpty()
			|| AgentFrameworkCppToolsetInternal::ContainsControlOrNewline(Prop.Type)
			|| AgentFrameworkCppToolsetInternal::ContainsControlOrNewline(Prop.DefaultValue)
			|| Prop.Type.Contains(TEXT(";")) || Prop.Type.Contains(TEXT("{")) || Prop.Type.Contains(TEXT("}")) || Prop.Type.Contains(TEXT("#"))
			|| Prop.DefaultValue.Contains(TEXT(";")) || Prop.DefaultValue.Contains(TEXT("{")) || Prop.DefaultValue.Contains(TEXT("}")) || Prop.DefaultValue.Contains(TEXT("#")))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Unsafe property type/default declaration on '%s'."), *Prop.Name));
			return Result;
		}

		FString SafetyReason;
		if (!ValidateCodeSafety(Prop.Type, SafetyReason) || !ValidateCodeSafety(Prop.DefaultValue, SafetyReason))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Property '%s' rejected: %s"), *Prop.Name, *SafetyReason));
			return Result;
		}

		if (Prop.bReplicated)
		{
			if (bIsStruct || bIsInterface)
			{
				UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Replicated property '%s' is invalid on struct/interface '%s'."), *Prop.Name, *PrefixedTypeName));
				return Result;
			}
			bHasReplicatedProps = true;
		}
	}

	if (bHasReplicatedProps)
	{
		CleanIncludes.AddUnique(TEXT("Net/UnrealNetwork.h"));
	}

	const TArray<FAgentFrameworkCppMethodSpec> MethodList = Methods.Get(TArray<FAgentFrameworkCppMethodSpec>{});
	for (const FAgentFrameworkCppMethodSpec& Method : MethodList)
	{
		if (bIsStruct)
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), TEXT("Cannot add UFUNCTION methods to a USTRUCT."));
			return Result;
		}
		if (!UAgentFrameworkActionUtils::IsValidCppIdentifier(Method.Name))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Invalid C++ method identifier '%s'."), *Method.Name));
			return Result;
		}
		if (Method.ReturnType.TrimStartAndEnd().IsEmpty()
			|| AgentFrameworkCppToolsetInternal::ContainsControlOrNewline(Method.ReturnType)
			|| AgentFrameworkCppToolsetInternal::ContainsControlOrNewline(Method.Parameters)
			|| Method.ReturnType.Contains(TEXT(";")) || Method.ReturnType.Contains(TEXT("{")) || Method.ReturnType.Contains(TEXT("#"))
			|| Method.Parameters.Contains(TEXT(";")) || Method.Parameters.Contains(TEXT("{")) || Method.Parameters.Contains(TEXT("#")))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Unsafe signature on method '%s'."), *Method.Name));
			return Result;
		}
		if (Method.Body.Contains(TEXT("#include")) || Method.Body.Contains(TEXT("#pragma")) || Method.Body.Contains(TEXT("#define")))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Preprocessor directives are not allowed in method body for '%s'."), *Method.Name));
			return Result;
		}
		if (!AgentFrameworkCppToolsetInternal::CheckBalancedBraces(Method.Body, Error))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Method '%s': %s"), *Method.Name, *Error));
			return Result;
		}
		FString SafetyReason;
		if (!ValidateCodeSafety(Method.ReturnType, SafetyReason)
			|| !ValidateCodeSafety(Method.Parameters, SafetyReason)
			|| !ValidateCodeSafety(Method.Body, SafetyReason))
		{
			UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Method '%s' rejected: %s"), *Method.Name, *SafetyReason));
			return Result;
		}
	}

	FString ResolvedModuleName;
	FString HeaderDir;
	FString SourceDir;
	FString IncludePrefix;
	FString ApiMacro;
	if (!UAgentFrameworkActionUtils::ResolveWritableModulePaths(
		ModuleName.Get(TEXT("")), SubDirectory.Get(TEXT("")),
		ResolvedModuleName, HeaderDir, SourceDir, IncludePrefix, ApiMacro, Error))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), Error);
		return Result;
	}

	const FString HeaderPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(HeaderDir, BaseFileName + TEXT(".h")));
	const FString SourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(SourceDir, BaseFileName + TEXT(".cpp")));

	if (!IsWritableSourcePath(HeaderPath) || !IsWritableSourcePath(SourcePath))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), TEXT("Target file paths are outside writable project Source/Plugin paths."));
		return Result;
	}
	if (FPaths::FileExists(HeaderPath) || FPaths::FileExists(SourcePath))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Files already exist for '%s'."), *BaseFileName));
		return Result;
	}

	// Build Header
	FString HeaderContent = TEXT("// Copyright AgentFramework. All Rights Reserved.\n\n#pragma once\n\n#include \"CoreMinimal.h\"\n");
	for (const FString& Inc : CleanIncludes)
	{
		HeaderContent += FString::Printf(TEXT("#include \"%s\"\n"), *Inc);
	}
	HeaderContent += FString::Printf(TEXT("#include \"%s.generated.h\"\n\n"), *BaseFileName);

	if (bIsStruct)
	{
		HeaderContent += TEXT("USTRUCT(BlueprintType)\n");
		HeaderContent += FString::Printf(TEXT("struct %s %s\n{\n\tGENERATED_BODY()\n\n"), *ApiMacro, *PrefixedTypeName);
	}
	else if (bIsInterface)
	{
		const FString InterfaceClassName = TEXT("I") + BaseFileName;
		HeaderContent += TEXT("UINTERFACE(MinimalAPI, BlueprintType)\n");
		HeaderContent += FString::Printf(TEXT("class %s : public UInterface\n{\n\tGENERATED_BODY()\n};\n\n"), *PrefixedTypeName);
		HeaderContent += FString::Printf(TEXT("class %s %s\n{\n\tGENERATED_BODY()\n\npublic:\n"), *ApiMacro, *InterfaceClassName);
	}
	else
	{
		HeaderContent += TEXT("UCLASS()\n");
		HeaderContent += FString::Printf(TEXT("class %s %s : public %s\n{\n\tGENERATED_BODY()\n\npublic:\n\t%s();\n"),
			*ApiMacro, *PrefixedTypeName, *PrefixedParent, *PrefixedTypeName);
		if (bHasReplicatedProps)
		{
			HeaderContent += TEXT("\tvirtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;\n");
		}
		HeaderContent += TEXT("\n");
	}

	for (const FAgentFrameworkCppPropertySpec& Prop : PropList)
	{
		const FString SafeCat = AgentFrameworkCppToolsetInternal::SanitizeCategoryString(Prop.Category);
		if (!Prop.Comment.IsEmpty())
		{
			HeaderContent += FString::Printf(TEXT("\t/** %s */\n"), *AgentFrameworkCppToolsetInternal::SanitizeSingleLineComment(Prop.Comment));
		}

		TArray<FString> Specifiers;
		if (Prop.bEditAnywhere) { Specifiers.Add(TEXT("EditAnywhere")); }
		if (Prop.bBlueprintReadWrite) { Specifiers.Add(TEXT("BlueprintReadWrite")); }
		if (Prop.bReplicated) { Specifiers.Add(TEXT("Replicated")); }
		Specifiers.Add(FString::Printf(TEXT("Category = \"%s\""), *SafeCat));

		HeaderContent += FString::Printf(TEXT("\tUPROPERTY(%s)\n"), *FString::Join(Specifiers, TEXT(", ")));
		if (Prop.DefaultValue.IsEmpty())
		{
			HeaderContent += FString::Printf(TEXT("\t%s %s;\n\n"), *Prop.Type.TrimStartAndEnd(), *Prop.Name);
		}
		else
		{
			HeaderContent += FString::Printf(TEXT("\t%s %s = %s;\n\n"), *Prop.Type.TrimStartAndEnd(), *Prop.Name, *Prop.DefaultValue.TrimStartAndEnd());
		}
	}

	for (const FAgentFrameworkCppMethodSpec& Method : MethodList)
	{
		const FString SafeCat = AgentFrameworkCppToolsetInternal::SanitizeCategoryString(Method.Category);
		if (!Method.Comment.IsEmpty())
		{
			HeaderContent += FString::Printf(TEXT("\t/** %s */\n"), *AgentFrameworkCppToolsetInternal::SanitizeSingleLineComment(Method.Comment));
		}
		TArray<FString> Specifiers;
		if (Method.bBlueprintCallable) { Specifiers.Add(TEXT("BlueprintCallable")); }
		Specifiers.Add(FString::Printf(TEXT("Category = \"%s\""), *SafeCat));

		HeaderContent += FString::Printf(TEXT("\tUFUNCTION(%s)\n"), *FString::Join(Specifiers, TEXT(", ")));
		const FString ConstQual = Method.bConst ? TEXT(" const") : TEXT("");
		const FString OverrideQual = (Method.bOverride && !bIsInterface) ? TEXT(" override") : TEXT("");
		HeaderContent += FString::Printf(TEXT("\t%s %s(%s)%s%s;\n\n"),
			*Method.ReturnType.TrimStartAndEnd(), *Method.Name, *Method.Parameters.TrimStartAndEnd(), *ConstQual, *OverrideQual);
	}

	HeaderContent += TEXT("};\n");

	// Build Source
	const FString CppInclude = IncludePrefix + BaseFileName + TEXT(".h");
	FString SourceContent = FString::Printf(TEXT("// Copyright AgentFramework. All Rights Reserved.\n\n#include \"%s\"\n\n"), *CppInclude);

	if (!bIsStruct && !bIsInterface)
	{
		SourceContent += FString::Printf(TEXT("%s::%s()\n{\n}\n\n"), *PrefixedTypeName, *PrefixedTypeName);

		if (bHasReplicatedProps)
		{
			SourceContent += FString::Printf(TEXT("void %s::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const\n{\n\tSuper::GetLifetimeReplicatedProps(OutLifetimeProps);\n"),
				*PrefixedTypeName);
			for (const FAgentFrameworkCppPropertySpec& Prop : PropList)
			{
				if (Prop.bReplicated)
				{
					SourceContent += FString::Printf(TEXT("\tDOREPLIFETIME(%s, %s);\n"), *PrefixedTypeName, *Prop.Name);
				}
			}
			SourceContent += TEXT("}\n\n");
		}
	}

	const FString OwnerClassScope = bIsInterface ? (TEXT("I") + BaseFileName) : PrefixedTypeName;
	for (const FAgentFrameworkCppMethodSpec& Method : MethodList)
	{
		const FString ConstQual = Method.bConst ? TEXT(" const") : TEXT("");
		SourceContent += FString::Printf(TEXT("%s %s::%s(%s)%s\n{\n"),
			*Method.ReturnType.TrimStartAndEnd(), *OwnerClassScope, *Method.Name, *Method.Parameters.TrimStartAndEnd(), *ConstQual);
		if (!Method.Body.TrimStartAndEnd().IsEmpty())
		{
			SourceContent += Method.Body + TEXT("\n");
		}
		else if (!Method.ReturnType.TrimStartAndEnd().Equals(TEXT("void")))
		{
			SourceContent += FString::Printf(TEXT("\treturn %s{};\n"), *Method.ReturnType.TrimStartAndEnd());
		}
		SourceContent += TEXT("}\n\n");
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(HeaderPath), true);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(SourcePath), true);

	if (!FFileHelper::SaveStringToFile(HeaderContent, *HeaderPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Failed to write header file '%s'."), *HeaderPath));
		return Result;
	}

	if (!FFileHelper::SaveStringToFile(SourceContent, *SourcePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Delete(*HeaderPath, false, true);
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("ScaffoldCppClass"), FString::Printf(TEXT("Failed to write source file '%s'; rolled back header."), *SourcePath));
		return Result;
	}

	Result.ClassName = BaseFileName;
	Result.PrefixedClassName = PrefixedTypeName;
	Result.ParentClassName = bIsStruct ? TEXT("UStruct") : PrefixedParent;
	Result.ModuleName = ResolvedModuleName;
	Result.HeaderPath = HeaderPath;
	Result.SourcePath = SourcePath;
	Result.bRequiresRebuild = !bIsStruct || PropList.Num() > 0;
	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("ScaffoldCppClass"), 0.0, true, {});
	return Result;
}

UAsyncFunctionResultString* UAgentFrameworkCppToolset::RegenerateProjectFiles(int32 TimeoutSeconds)
{
	if (TimeoutSeconds <= 0)
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("TimeoutSeconds must be > 0 (got %d)."), TimeoutSeconds));
	}

	const FString ProjectFile = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());
	if (ProjectFile.IsEmpty() || !FPaths::FileExists(ProjectFile))
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("Could not locate valid .uproject file (resolved '%s')."), *ProjectFile));
	}

	FString Executable;
	FString Params;
	const FString RocketFlag = FApp::IsEngineInstalled() ? TEXT("-rocket ") : TEXT("");

#if PLATFORM_WINDOWS
	Executable = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe"));
	Params = FString::Printf(TEXT("-projectfiles -project=\"%s\" -game %s-progress"), *ProjectFile, *RocketFlag);
#elif PLATFORM_MAC
	const FString NativeUbt = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool"));
	if (FPaths::FileExists(NativeUbt))
	{
		Executable = NativeUbt;
		Params = FString::Printf(TEXT("-projectfiles -project=\"%s\" -game %s-progress"), *ProjectFile, *RocketFlag);
	}
	else
	{
		Executable = TEXT("/bin/bash");
		const FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/Mac/GenerateProjectFiles.sh"));
		Params = FString::Printf(TEXT("\"%s\" -project=\"%s\" -game %s-progress"), *ScriptPath, *ProjectFile, *RocketFlag);
	}
#elif PLATFORM_LINUX
	const FString NativeUbt = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/DotNET/UnrealBuildTool/UnrealBuildTool"));
	if (FPaths::FileExists(NativeUbt))
	{
		Executable = NativeUbt;
		Params = FString::Printf(TEXT("-projectfiles -project=\"%s\" -game %s-progress"), *ProjectFile, *RocketFlag);
	}
	else
	{
		Executable = TEXT("/usr/bin/env");
		const FString ScriptPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Build/BatchFiles/Linux/Build.sh"));
		Params = FString::Printf(TEXT(" -- \"%s\" -projectfiles -project=\"%s\" -game %s-progress"), *ScriptPath, *ProjectFile, *RocketFlag);
	}
#endif

	if (Executable.IsEmpty() || (!Executable.StartsWith(TEXT("/")) && !FPaths::FileExists(Executable)))
	{
		return UE::AsyncFunctionResult::MakeErrorResult<UAsyncFunctionResultString>(
			FString::Printf(TEXT("UnrealBuildTool executable not found at '%s'."), *Executable));
	}

	return UAgentFrameworkActionUtils::RunMonitoredProcessAsync(
		TEXT("RegenerateProjectFiles"),
		Executable,
		Params,
		static_cast<float>(TimeoutSeconds),
		[ProjectFile](int32 ExitCode, const FString& Output, FString& OutValue, FString& OutError)
		{
			if (ExitCode == 0)
			{
				OutValue = FString::Printf(TEXT("Regenerated project files for '%s'.\n%s"), *ProjectFile, *Output);
			}
			else
			{
				OutError = FString::Printf(TEXT("RegenerateProjectFiles failed with exit code %d for '%s'.\n%s"), ExitCode, *ProjectFile, *Output);
			}
		});
}

FAgentFrameworkCppReflectionInfo UAgentFrameworkCppToolset::GetCppReflectionInfo(
	const FString& TypeName,
	bool bIncludeInherited)
{
	FAgentFrameworkCppReflectionInfo Info;
	const FString TrimmedType = TypeName.TrimStartAndEnd();
	if (TrimmedType.IsEmpty())
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("GetCppReflectionInfo"), TEXT("TypeName cannot be empty."));
		return Info;
	}

	struct FMatchedType
	{
		UField* Field = nullptr;
		FString Kind;
	};

	TArray<FMatchedType> ExactMatches;
	TArray<FMatchedType> PrefixedMatches;
	bool bFoundByFullPath = false;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Cls = *It;
		if (AgentFrameworkCppToolsetInternal::IsStaleOrInternalClass(Cls))
		{
			continue;
		}
		if (Cls->GetPathName().Equals(TrimmedType, ESearchCase::IgnoreCase))
		{
			ExactMatches = { { Cls, TEXT("Class") } };
			bFoundByFullPath = true;
			break;
		}
		if (Cls->GetName().Equals(TrimmedType, ESearchCase::IgnoreCase))
		{
			ExactMatches.Add({ Cls, TEXT("Class") });
		}
		else if (Cls->GetName().Equals(TEXT("A") + TrimmedType, ESearchCase::IgnoreCase)
			|| Cls->GetName().Equals(TEXT("U") + TrimmedType, ESearchCase::IgnoreCase))
		{
			PrefixedMatches.Add({ Cls, TEXT("Class") });
		}
	}

	if (!bFoundByFullPath)
	{
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* Struct = *It;
			if (!Struct || Struct->GetName().StartsWith(TEXT("TRASH_")) || Struct->GetName().StartsWith(TEXT("REINST_")))
			{
				continue;
			}
			if (Struct->GetPathName().Equals(TrimmedType, ESearchCase::IgnoreCase))
			{
				ExactMatches = { { Struct, TEXT("Struct") } };
				bFoundByFullPath = true;
				break;
			}
			if (Struct->GetName().Equals(TrimmedType, ESearchCase::IgnoreCase))
			{
				ExactMatches.Add({ Struct, TEXT("Struct") });
			}
			else if (Struct->GetName().Equals(TEXT("F") + TrimmedType, ESearchCase::IgnoreCase))
			{
				PrefixedMatches.Add({ Struct, TEXT("Struct") });
			}
		}
	}

	if (!bFoundByFullPath)
	{
		for (TObjectIterator<UEnum> It; It; ++It)
		{
			UEnum* Enum = *It;
			if (!Enum)
			{
				continue;
			}
			if (Enum->GetPathName().Equals(TrimmedType, ESearchCase::IgnoreCase))
			{
				ExactMatches = { { Enum, TEXT("Enum") } };
				bFoundByFullPath = true;
				break;
			}
			if (Enum->GetName().Equals(TrimmedType, ESearchCase::IgnoreCase))
			{
				ExactMatches.Add({ Enum, TEXT("Enum") });
			}
			else if (Enum->GetName().Equals(TEXT("E") + TrimmedType, ESearchCase::IgnoreCase))
			{
				PrefixedMatches.Add({ Enum, TEXT("Enum") });
			}
		}
	}

	const TArray<FMatchedType>& Candidates = ExactMatches.Num() > 0 ? ExactMatches : PrefixedMatches;
	if (Candidates.Num() == 0)
	{
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("GetCppReflectionInfo"), FString::Printf(TEXT("Type '%s' not found in loaded UClass, UScriptStruct, or UEnum reflection."), *TrimmedType));
		return Info;
	}
	if (Candidates.Num() > 1)
	{
		TArray<FString> Paths;
		for (const FMatchedType& M : Candidates)
		{
			Paths.Add(M.Field->GetPathName());
		}
		UAgentFrameworkActionUtils::RaiseToolError(TEXT("GetCppReflectionInfo"),
			FString::Printf(TEXT("Ambiguous type '%s' matched multiple live types (%s). Provide a full path."), *TrimmedType, *FString::Join(Paths, TEXT(", "))));
		return Info;
	}

	const FMatchedType& Match = Candidates[0];
	Info.TypeName = Match.Field->GetName();
	Info.FullPath = Match.Field->GetPathName();
	Info.TypeKind = Match.Kind;
	Info.ModulePackage = Match.Field->GetOutermost()->GetName();

	const EFieldIteratorFlags::SuperClassFlags SuperFlags = bIncludeInherited
		? EFieldIteratorFlags::IncludeSuper
		: EFieldIteratorFlags::ExcludeSuper;

	auto PopulatePropertyList = [SuperFlags](const UStruct* StructType, TArray<FAgentFrameworkCppReflectedProperty>& OutProps)
	{
		for (TFieldIterator<FProperty> PropIt(StructType, SuperFlags); PropIt; ++PropIt)
		{
			FProperty* Prop = *PropIt;
			FAgentFrameworkCppReflectedProperty Entry;
			Entry.Name = Prop->GetName();
			Entry.Type = Prop->GetCPPType();
			Entry.bEditAnywhere = Prop->HasAnyPropertyFlags(CPF_Edit);
			Entry.bBlueprintReadWrite = Prop->HasAllPropertyFlags(CPF_BlueprintVisible) && !Prop->HasAnyPropertyFlags(CPF_BlueprintReadOnly);
			Entry.bBlueprintReadOnly = Prop->HasAllPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintReadOnly);
			Entry.bReplicated = Prop->HasAnyPropertyFlags(CPF_Net);
#if WITH_METADATA
			Entry.Category = Prop->GetMetaData(TEXT("Category"));
			if (const TMap<FName, FString>* MetaMap = Prop->GetMetaDataMap())
			{
				for (const TPair<FName, FString>& Pair : *MetaMap)
				{
					Entry.MetaData.Add(Pair.Key.ToString(), Pair.Value);
				}
			}
#endif
			OutProps.Add(MoveTemp(Entry));
		}
	};

	if (UClass* Cls = Cast<UClass>(Match.Field))
	{
		if (UClass* SuperCls = Cls->GetSuperClass())
		{
			Info.ParentClassName = SuperCls->GetName();
		}
		FString HeaderPath;
		if (FSourceCodeNavigation::FindClassHeaderPath(Cls, HeaderPath))
		{
			Info.HeaderPath = HeaderPath;
		}
		if (Cls->HasAnyClassFlags(CLASS_Abstract)) { Info.Flags.Add(TEXT("Abstract")); }
		if (FKismetEditorUtilities::CanCreateBlueprintOfClass(Cls)) { Info.Flags.Add(TEXT("Blueprintable")); }
		if (Cls->HasAnyClassFlags(CLASS_Native)) { Info.Flags.Add(TEXT("Native")); }
		if (Cls->HasAnyClassFlags(CLASS_Config)) { Info.Flags.Add(TEXT("Config")); }

		for (const FImplementedInterface& Iface : Cls->Interfaces)
		{
			if (Iface.Class)
			{
				Info.Interfaces.Add(Iface.Class->GetName());
			}
		}

		PopulatePropertyList(Cls, Info.Properties);

		for (TFieldIterator<UFunction> FuncIt(Cls, SuperFlags); FuncIt; ++FuncIt)
		{
			UFunction* Func = *FuncIt;
			FAgentFrameworkCppReflectedFunction FuncEntry;
			FuncEntry.Name = Func->GetName();
			if (Func->HasAnyFunctionFlags(FUNC_BlueprintCallable)) { FuncEntry.Flags.Add(TEXT("BlueprintCallable")); }
			if (Func->HasAnyFunctionFlags(FUNC_BlueprintPure)) { FuncEntry.Flags.Add(TEXT("BlueprintPure")); }
			if (Func->HasAnyFunctionFlags(FUNC_BlueprintEvent)) { FuncEntry.Flags.Add(TEXT("BlueprintEvent")); }
			if (Func->HasAnyFunctionFlags(FUNC_Static)) { FuncEntry.Flags.Add(TEXT("Static")); }
			if (Func->HasAnyFunctionFlags(FUNC_Exec)) { FuncEntry.Flags.Add(TEXT("Exec")); }
			if (Func->HasAnyFunctionFlags(FUNC_Const)) { FuncEntry.Flags.Add(TEXT("Const")); }

			for (TFieldIterator<FProperty> ParamIt(Func); ParamIt; ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (!Param->HasAnyPropertyFlags(CPF_Parm))
				{
					continue;
				}
				if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					FuncEntry.ReturnType = Param->GetCPPType();
				}
				FAgentFrameworkCppReflectedParam ParamEntry;
				ParamEntry.Name = Param->GetName();
				ParamEntry.Type = Param->GetCPPType();
				ParamEntry.bReturnParam = Param->HasAnyPropertyFlags(CPF_ReturnParm);
				ParamEntry.bOutParam = Param->HasAnyPropertyFlags(CPF_OutParm) && !Param->HasAnyPropertyFlags(CPF_ConstParm);
				FuncEntry.Parameters.Add(MoveTemp(ParamEntry));
			}

			Info.Functions.Add(MoveTemp(FuncEntry));
		}
	}
	else if (UScriptStruct* Struct = Cast<UScriptStruct>(Match.Field))
	{
		if (UStruct* SuperStruct = Struct->GetSuperStruct())
		{
			Info.ParentClassName = SuperStruct->GetName();
		}
		PopulatePropertyList(Struct, Info.Properties);
	}
	else if (UEnum* Enum = Cast<UEnum>(Match.Field))
	{
		const int32 EnumCount = Enum->ContainsExistingMax() ? (Enum->NumEnums() - 1) : Enum->NumEnums();
		for (int32 Idx = 0; Idx < EnumCount; ++Idx)
		{
			FAgentFrameworkCppReflectedEnumValue EnumEntry;
			EnumEntry.Name = Enum->GetNameStringByIndex(Idx);
			EnumEntry.Value = Enum->GetValueByIndex(Idx);
			EnumEntry.DisplayName = Enum->GetDisplayNameTextByIndex(Idx).ToString();
			Info.EnumValues.Add(MoveTemp(EnumEntry));
		}
	}

	UAgentFrameworkActionUtils::RecordToolExecution(TEXT("GetCppReflectionInfo"), 0.0, true, {});
	return Info;
}
