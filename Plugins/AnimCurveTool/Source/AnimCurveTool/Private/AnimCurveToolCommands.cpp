// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimCurveToolCommands.h"

#define LOCTEXT_NAMESPACE "FAnimCurveToolModule"

void FAnimCurveToolCommands::RegisterCommands()
{
	UI_COMMAND(OpenPluginWindow, "AnimCurveTool", "Bring up AnimCurveTool window", EUserInterfaceActionType::Button, FInputGesture());
}

#undef LOCTEXT_NAMESPACE
