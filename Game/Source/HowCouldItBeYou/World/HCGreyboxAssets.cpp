#include "World/HCGreyboxAssets.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/SoftObjectPath.h"

namespace HCGreybox
{
	template <typename T>
	static T* LoadCached(const TCHAR* Path)
	{
		// Works both inside constructors (CDO creation) and at runtime.
		return Cast<T>(FSoftObjectPath(Path).TryLoad());
	}

	UStaticMesh* Cube()
	{
		static TWeakObjectPtr<UStaticMesh> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadCached<UStaticMesh>(TEXT("/Engine/BasicShapes/Cube.Cube"));
		}
		return Cached.Get();
	}

	UStaticMesh* Cylinder()
	{
		static TWeakObjectPtr<UStaticMesh> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadCached<UStaticMesh>(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		}
		return Cached.Get();
	}

	UStaticMesh* Sphere()
	{
		static TWeakObjectPtr<UStaticMesh> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadCached<UStaticMesh>(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
		}
		return Cached.Get();
	}

	UMaterialInterface* BaseMaterial()
	{
		static TWeakObjectPtr<UMaterialInterface> Cached;
		if (!Cached.IsValid())
		{
			Cached = LoadCached<UMaterialInterface>(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
		}
		return Cached.Get();
	}

	UMaterialInstanceDynamic* MakeColored(UObject* Outer, const FLinearColor& Color)
	{
		UMaterialInterface* Base = BaseMaterial();
		if (!Base)
		{
			return nullptr;
		}
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Base, Outer);
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		return MID;
	}
}
