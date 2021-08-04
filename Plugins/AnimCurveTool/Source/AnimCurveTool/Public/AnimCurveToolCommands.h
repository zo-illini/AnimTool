// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/Commands.h"
#include "AnimCurveToolStyle.h"

class FAnimCurveToolCommands : public TCommands<FAnimCurveToolCommands>
{
public:

	FAnimCurveToolCommands()
		: TCommands<FAnimCurveToolCommands>(TEXT("AnimCurveTool"), NSLOCTEXT("Contexts", "AnimCurveTool", "AnimCurveTool Plugin"), NAME_None, FAnimCurveToolStyle::GetStyleSetName())
	{
	}

	// TCommands<> interface
	virtual void RegisterCommands() override;

public:
	TSharedPtr< FUICommandInfo > OpenPluginWindow;
};