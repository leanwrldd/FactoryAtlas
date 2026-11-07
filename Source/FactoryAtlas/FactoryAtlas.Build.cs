using UnrealBuildTool;

public class FactoryAtlas : ModuleRules
{
	public FactoryAtlas(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject",
			"Engine",
			"InputCore",
			"Slate", "SlateCore", "UMG",
		});

		// CreateInstanceBuffer is reached through the renderer module's interface at runtime, so only its
		// header is needed. Linking it would break dedicated server builds, which ship without the module.
		PrivateIncludePathModuleNames.Add("SlateRHIRenderer");
		PrivateDependencyModuleNames.AddRange(new string[] { "ImageWrapper", "AbstractInstance" });

		// Header stubs
		PublicDependencyModuleNames.AddRange(new string[] {
			"DummyHeaders",
		});

		PublicDependencyModuleNames.AddRange(new string[] {"FactoryGame", "SML"});
	}
}
