#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UObject;

/** Engine basic shapes used for the greybox, so the prototype needs no content assets. */
namespace HCGreybox
{
	HOWCOULDITBEYOU_API UStaticMesh* Cube();
	HOWCOULDITBEYOU_API UStaticMesh* Cylinder();
	HOWCOULDITBEYOU_API UStaticMesh* Sphere();
	HOWCOULDITBEYOU_API UMaterialInterface* BaseMaterial();

	/** Dynamic instance of the basic shape material with the "Color" parameter set. */
	HOWCOULDITBEYOU_API UMaterialInstanceDynamic* MakeColored(UObject* Outer, const FLinearColor& Color);
}
