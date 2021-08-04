// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimCurveTool.h"

#include <string>
#include <Windows.ApplicationModel.Contacts.h>


#include "AnimCurveToolStyle.h"
#include "AnimCurveToolCommands.h"
#include "IMessageTracer.h"
#include "LevelEditor.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "ToolMenus.h"
#include "Animation/AnimNodeBase.h"
#include "Components/SplineComponent.h"

static const FName AnimCurveToolTabName("AnimTool");

#define LOCTEXT_NAMESPACE "FAnimCurveToolModule"

SGMarkerReference::SGMarkerReference(UAnimSequence * Anim, FName LeftFoot, FName RightFoot)
{
	AnimSequence = Anim;
	bIsValid = false;
	Dir = GetAnimDirection();

	// 计算各个动画的步态基准点
	LeftMarkers = GetContactTimeFromTurning(AnimSequence, LeftFoot);
	LeftMarkers.Sort();

	RightMarkers = GetContactTimeFromTurning(AnimSequence, RightFoot);
	RightMarkers.Sort();

	// 基准点不合法的情况
	if (LeftMarkers.Num() == 0 || RightMarkers.Num() == 0 || LeftMarkers.Num() != RightMarkers.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("Reference group calculation failed for %s: left: %d, right: %d"), *Anim->GetName(), LeftMarkers.Num(), RightMarkers.Num());
		return;
	}
	
	Intervals.Reset();
	if (LeftMarkers[0] < RightMarkers[0])
	{
		for (int i = 0; i < LeftMarkers.Num(); i++)
		{
			Intervals.Add(FootInterval(LeftMarkers[i], RightMarkers[i], true));
			if (i == LeftMarkers.Num()-1)
			{
				// 区间横跨了两个动画循环的情况
				Intervals.Add(FootInterval(RightMarkers[i], LeftMarkers[0], false));
			}
			else
			{
				Intervals.Add(FootInterval(RightMarkers[i], LeftMarkers[i+1], false));
			}
		}
	}
	else
	{
		for (int i = 0; i < LeftMarkers.Num(); i++)
        	{
        		Intervals.Add(FootInterval(RightMarkers[i], LeftMarkers[i], false));
        		if (i == LeftMarkers.Num()-1)
        		{
        			// 区间横跨了两个动画循环的情况
        			Intervals.Add(FootInterval(LeftMarkers[i], RightMarkers[0], true));
        		}
        		else
        		{
        			Intervals.Add(FootInterval(LeftMarkers[i], RightMarkers[i+1], true));
        		}
        	}
	}
	bIsValid = true;
}

Direction SGMarkerReference::GetAnimDirection()
{
	const FString AnimName = AnimSequence->GetName();

	// 根据动画名称进行标签
	if (AnimName.EndsWith(FString("FL")))
	{
		return Direction::lf;
	}
	else if (AnimName.EndsWith(FString("FR")))
	{
		return Direction::rf;
	}
	else if (AnimName.EndsWith(FString("BL")))
	{
		return Direction::lb;
	}
	else if (AnimName.EndsWith(FString("BR")))
	{
		return Direction::rb;
	}
	
	if (AnimName.EndsWith(FString("F")))
	{
		return Direction::f;
	}
	else if (AnimName.EndsWith(FString("B")))
	{
		return Direction::b;
	}
	else if (AnimName.EndsWith(FString("R")))
	{
		return Direction::r;
	}
	else if (AnimName.EndsWith(FString("L")))
	{
		return Direction::l;
	}

	UE_LOG(LogTemp, Warning, TEXT("No Direction Assigned for %s. Check Naming Convention."), *AnimSequence->GetName());
	return Direction::f;
}

bool SGMarkerReference::GetRatioFromTime(float Time, float & RefRatio, bool & IsOrderLeftRight)
{
	FootInterval target = Intervals[Intervals.Num()-1];

	// 找到目标时间所在的基准区间
	for (FootInterval interval : Intervals)
	{
		if (Time > interval.Left && Time < interval.Right)
		{
			target = interval;
		}
	}

	// 计算目标时间在所在基准区间中的比例
	if (!target.IsWrapped)
	{
		RefRatio = (Time - target.Left) / (target.Right - target.Left);
	}
	else
	{
		// 如果区间横跨了两个循环，则要先为右界加上循环长度再减去左界
		if (Time < target.Left)
			Time += AnimSequence->GetPlayLength();
		RefRatio = (Time - target.Left) / (target.Right + AnimSequence->GetPlayLength() - target.Left);
	}
	IsOrderLeftRight = target.IsOrderLeftRight;

	return true;	
}

bool SGMarkerReference::GetTimeFromRatio(float RefRatio, bool IsOrderLeftRight, TArray<float> & Time)
{
	Time.Reset();
	const float Len = AnimSequence->GetPlayLength();
	for (FootInterval Interval : Intervals)
	{
		// 根据先左后右或先右后左的顺序，筛选区间
		if (Interval.IsOrderLeftRight == IsOrderLeftRight)
		{
			if (!Interval.IsWrapped)
			{
				Time.Add((Interval.Left + (Interval.Right - Interval.Left) * RefRatio));
			}
			else
			{
				// 区间横跨了两个动画循环的情况
				float Loc =(Interval.Left + (Interval.Right - Interval.Left + Len) * RefRatio);
				Time.Add(Loc > Len ? Loc - Len : Loc);
			}
		}
	}
	return true;
}

TArray<float> SGMarkerReference::GetContactTimeFromTurning(UAnimSequence* AnimationSequence, FName BoneName)
{
	int NumFrame = AnimationSequence->GetNumberOfFrames()-1;
	float Threshold = 0.25;
	
	FTransform LastFrameTransform, CurFrameTransform, NextFrameTransform;
	TArray<float> TurningPoints, Results;

	// 遍历每一帧的位置数据，找到方向转折点
	for (int i = 0; i < NumFrame; i++)
	{
		int l = (i == 0) ? NumFrame-1 : i-1;
		int n = (i == NumFrame-1) ? 0 : i+1;
		
		LastFrameTransform = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, l);
		CurFrameTransform = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, i);
		NextFrameTransform = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, n);
		
		if (IsTurningPoint(LastFrameTransform, CurFrameTransform, NextFrameTransform))
		{
			TurningPoints.Add(i);
		}
	}

	// 检查所有方向转折点的之后几帧，找到稳定低高度的点
	for (float t : TurningPoints)
	{
		int n;
		while (true)
		{
			n = (t == NumFrame-1) ? 0 : t+1;
			CurFrameTransform = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, t);
			NextFrameTransform = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, n);

			// 脚部的下降幅度小于阈值（或者已为负数），进行标记
			if (CurFrameTransform.GetLocation().Z - NextFrameTransform.GetLocation().Z < Threshold)
			{
				Results.Add(AnimationSequence->GetTimeAtFrame(n));
				UE_LOG(LogTemp, Warning, TEXT("%s %s: %d"), *AnimationSequence->GetName(), *BoneName.ToString(), n);
				break;
			}
			//否则先不进行标记，等待一个更稳定的低点
			else
			{
				t = n;
			}
		}
	}
	
	return Results;
}

bool SGMarkerReference::IsTurningPoint(FTransform LastFrame, FTransform CurFrame, FTransform NextFrame)
{
	FVector l = LastFrame.GetLocation();
	FVector c = CurFrame.GetLocation();
	FVector n = NextFrame.GetLocation();
	
	if (Dir == Direction::l)
	{
		return l.X < c.X && c.X > n.X;
	}
	else if (Dir == Direction::r)
	{
		return l.X > c.X && c.X < n.X;
	}

	if (Dir == Direction::f || Dir == Direction::lf ||Dir == Direction::rf)
	{
		return l.Y < c.Y && c.Y > n.Y;
	}
	else
	{
		return l.Y > c.Y && c.Y < n.Y;
	}
}


void FAnimCurveToolModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FAnimCurveToolStyle::Initialize();
	FAnimCurveToolStyle::ReloadTextures();

	FAnimCurveToolCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	PluginCommands->MapAction(
		FAnimCurveToolCommands::Get().OpenPluginWindow,
		FExecuteAction::CreateRaw(this, &FAnimCurveToolModule::PluginButtonClicked),
		FCanExecuteAction());

	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAnimCurveToolModule::RegisterMenus));

	// 绑定生成工具ui的回调函数
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AnimCurveToolTabName, FOnSpawnTab::CreateRaw(this, &FAnimCurveToolModule::OnSpawnPluginTab))
		.SetDisplayName(LOCTEXT("FAnimCurveToolTabTitle", "AnimTool"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	// 初始化成员的入口
	InitializeMembers();
}

void FAnimCurveToolModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	UToolMenus::UnRegisterStartupCallback(this);

	UToolMenus::UnregisterOwner(this);

	FAnimCurveToolStyle::Shutdown();

	FAnimCurveToolCommands::Unregister();

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AnimCurveToolTabName);
}

void FAnimCurveToolModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);

	{
		UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
		{
			FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
			Section.AddMenuEntryWithCommandList(FAnimCurveToolCommands::Get().OpenPluginWindow, PluginCommands);
		}
	}

	{
		UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar");
		{
			FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("Settings");
			{
				FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FAnimCurveToolCommands::Get().OpenPluginWindow));
				Entry.SetCommandList(PluginCommands);
			}
		}
	}
}

TSharedRef<SDockTab> FAnimCurveToolModule::OnSpawnPluginTab(const FSpawnTabArgs& SpawnTabArgs)
{
	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().HAlign(HAlign_Center).AutoWidth()
            [
                SNew(SBorder)
                [
                    MakeAnimPicker()
                ]
            ]
			/* 不再使用的曲线提取工具
			+ SHorizontalBox::Slot().HAlign(HAlign_Center).AutoWidth()
			[
				SNew(SBorder)
				[
					MakeCurveExtractor()      	
				]
			]
			*/
			+ SHorizontalBox::Slot().HAlign(HAlign_Center).AutoWidth()
            [
	            SNew(SBorder)
	            [
					MakePlaySpeedScaler()
	            ]

            ]
			
            + SHorizontalBox::Slot().HAlign(HAlign_Center).AutoWidth()
            [
                SNew(SBorder)
                [
		            MakeSGMarkerWidget()  	
                ]
            ]
		];

	return SpawnedTab;
}

void FAnimCurveToolModule::PluginButtonClicked()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AnimCurveToolTabName);
}

void FAnimCurveToolModule::InitializeMembers()
{
	/* 曲线提取工具相关的成员，已经不再使用了
	TransScale = FText::FromString("1");
	RotScale = FText::FromString("1");
	SavePath = "/Game";
	AnimSequenceToExtract = nullptr;
	
	OutputBoxStateMap.Add("TranslationX", false);
	OutputBoxStateMap.Add("TranslationY", false);
	OutputBoxStateMap.Add("TranslationZ", false);
	OutputBoxStateMap.Add("TranslationAll", false);
	OutputBoxStateMap.Add("RotationX", false);
	OutputBoxStateMap.Add("RotationY", false);
	OutputBoxStateMap.Add("RotationZ", false);
	OutputBoxStateMap.Add("RotationAll", false);
	OutputBoxStateMap.Add("ScaleX", false);
	OutputBoxStateMap.Add("ScaleY", false);
	OutputBoxStateMap.Add("ScaleZ", false);
	OutputBoxStateMap.Add("ScaleAll", false);
	*/

	FootLeft = FName(*FString("LeftToeBase"));
	FootRight = FName(*FString("RightToeBase"));

	ContactTolerance = FText::FromString("0.5");
}

TSharedRef<SWidget> FAnimCurveToolModule::MakeAnimPicker()
{
	TSharedRef<SWidget> AnimPicker =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 20)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Select Animation to be Processed   "))
            .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
        ]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 0, 15, 20)
		[
			SNew(SButton)
			.Text(FText::FromString("Add From Content Browser"))
			.OnClicked_Raw(this, &FAnimCurveToolModule::AddFromContentBrowser)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(15, 0, 15, 20)
    	[
    	    SNew(SButton)
    	    .Text(FText::FromString("Reset Group"))
    	    .OnClicked_Raw(this, &FAnimCurveToolModule::ResetSelectedAnimGroup)
    	]
    	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(20, 0, 20, 20)
    	[
    	    SNew(STextBlock)
    	    .Text(FText::FromString("Current Group"))
    	]
    	+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(20, 0, 20, 0)
    	[
    	    SAssignNew(SelectedAnimGroupPreview, STextBlock)
    	    .Text(FText::FromString("None"))
    	    .AutoWrapText(true)
    	];

	return AnimPicker;
}

FReply FAnimCurveToolModule::AddFromContentBrowser()
{
	TArray<FAssetData> Data;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>( "ContentBrowser" ).Get().GetSelectedAssets(Data);
	for (FAssetData & d : Data)
	{
		UAnimSequence * Anim = Cast<UAnimSequence>(d.GetAsset());
		if (Anim && SelectedAnimGroup.Find(Anim) == INDEX_NONE)
		{
			SelectedAnimGroup.Add(Anim);			
		}
	}
	UpdatePreviewText(SelectedAnimGroup, SelectedAnimGroupPreview.ToSharedRef());
	UpdateAnimGroupToScale();
	return FReply::Handled();
}

FReply FAnimCurveToolModule::ResetSelectedAnimGroup()
{
	SelectedAnimGroup.Reset();
	UpdateAnimGroupToScale();
	UpdatePreviewText(SelectedAnimGroup, SelectedAnimGroupPreview.ToSharedRef());
	return FReply::Handled();
}

TSharedRef<SWidget> FAnimCurveToolModule::MakePlaySpeedScaler()
{
	/*
	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateRaw(this, &FAnimCurveToolModule::OnAnimToScalePathSelected);
	PathPickerConfig.DefaultPath = "/Game/";
	*/
	
	TSharedRef<SWidget> SelectedAnimPreview =
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(20, 0, 20, 20)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Animation List"))
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(20, 0, 20, 0)
        [
            SAssignNew(AnimSequencesToScalePreview, STextBlock)
            .Text(FText::FromString("None"))
            .AutoWrapText(true)
        ];
	
	TSharedRef<SWidget> SpeedScaler =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(15, 0, 15, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().VAlign(VAlign_Center).Padding(0, 10, 0, 10).MaxHeight(32)
			[
				SNew(STextBlock)
				.Text(FText::FromString("Scale Animation PlayRate"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)
               [
                   SNew(STextBlock)
                   .Text(FText::FromString("Prefix Filter"))
               ]
			+ SVerticalBox::Slot().AutoHeight()
               [
                   SNew(SEditableTextBox)
                   .Text_Raw(this, &FAnimCurveToolModule::GetAnimPrefix)
                   .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnAnimPrefixCommitted)
               ]
			+ SVerticalBox::Slot().AutoHeight()
               [
                   SNew(STextBlock)
                   .Text(FText::FromString("Postfix Filter"))
               ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
               [
                   SNew(SEditableTextBox)
                   .Text_Raw(this, &FAnimCurveToolModule::GetAnimPostfix)
                   .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnAnimPostfixCommitted)
               ]
            + SVerticalBox::Slot().AutoHeight()
                [
                     SNew(STextBlock)
                     .Text(FText::FromString("Play Rate Scale"))
                ]
            + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SEditableTextBox)
                    .Text_Raw(this, &FAnimCurveToolModule::GetRateScale)
                    .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnRateScaleCommitted)
                ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
                [
                     SNew(SButton)
                     .OnClicked_Raw(this, &FAnimCurveToolModule::ApplyRateScale)
                     .Text(FText::FromString("Apply Play Rate Scale"))
                ]
            + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SEditableTextBox)
                    .MinDesiredWidth(50)
                    .Text_Raw(this, &FAnimCurveToolModule::GetRootMotionSpeed)
                    .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnRootMotionCommitted)
                ]
            + SVerticalBox::Slot().AutoHeight()
                [
                     SNew(SButton)
                     .OnClicked_Raw(this, &FAnimCurveToolModule::ApplyRootMotionSpeed)
                     .Text(FText::FromString("Apply Root Motion Speed"))
                ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 20, 0, 0)
            [
                SelectedAnimPreview		
            ]
            
			/*
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
        	[
        	    ContentBrowser.CreatePathPicker(PathPickerConfig)
        	]
        	*/
		];
	
	return SpeedScaler;
}

FReply FAnimCurveToolModule::ApplyRootMotionSpeed() const
{
	for (UAnimSequence * Anim : AnimSequencesToScale)
	{
		FVector translation = Anim->ExtractRootMotion(0, Anim->SequenceLength, false).GetTranslation();		
		if (!translation.IsNearlyZero(0.1))
		{
			float Speed = translation.Size() / Anim->SequenceLength;
			Anim->RateScale = FCString::Atof(*RootMotionSpeed.ToString()) / Speed;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Animation %s has no root motion. Skipping."), *Anim->GetName());
		}
	}

	return FReply::Handled();
}

bool FAnimCurveToolModule::CheckShouldSelectAnim(FAssetData Asset) const
{
	FString Prefix = AnimPrefix.ToString();
	FString Postfix = AnimPostfix.ToString();
			
	return ( (Prefix.Len() == 0 || Asset.AssetName.ToString().StartsWith(AnimPrefix.ToString(), ESearchCase::CaseSensitive))
        && (Postfix.Len() == 0  || Asset.AssetName.ToString().EndsWith(AnimPostfix.ToString(), ESearchCase::CaseSensitive)));
}

bool FAnimCurveToolModule::CheckShouldSelectAnim(UAnimSequence* AnimSequence) const
{
	FString Prefix = AnimPrefix.ToString();
	FString Postfix = AnimPostfix.ToString();
			
	return ( (Prefix.Len() == 0 || AnimSequence->GetName().StartsWith(AnimPrefix.ToString(), ESearchCase::CaseSensitive))
        && (Postfix.Len() == 0  || AnimSequence->GetName().EndsWith(AnimPostfix.ToString(), ESearchCase::CaseSensitive)));
}

FReply FAnimCurveToolModule::ApplyRateScale() const
{
	for (UAnimSequence * Anim : AnimSequencesToScale)
	{
		Anim->RateScale = FCString::Atof(*RateScale.ToString());
	}
	return FReply::Handled();
}

/*
void FAnimCurveToolModule::OnSavePathSelected(const FString& NewPath)
{
	SavePath = NewPath;
}
void FAnimCurveToolModule::OnAnimToScalePathSelected(const FString& NewPath)
{
	AnimToScalePath = NewPath;
	UpdateFilteredAnim(AnimSequencesToScale, AnimSequencesToScalePreview.ToSharedRef(), AnimToScalePath);
}

bool FAnimCurveToolModule::UpdateFilteredAnim(TArray<UAnimSequence*> & TargetAnimSequences, TSharedRef<STextBlock> PreviewTextWidget, FString path)
{
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter filter;
	filter.PackagePaths.Add(FName(*path));
	//filter.bRecursivePaths = true;
	filter.ClassNames.Add(FName("AnimSequence"));
	filter.bRecursiveClasses = true;
	TArray<FAssetData> SelectedAnimSequence;

	if(AssetRegistryModule.Get().GetAssets(filter, SelectedAnimSequence))
	{
		TargetAnimSequences.Reset();
		for (FAssetData & data : SelectedAnimSequence)
		{
			if (CheckShouldSelectAnim(data))
			{
				UAnimSequence * AnimPtr = Cast<UAnimSequence>(data.GetAsset());
				TargetAnimSequences.Add(AnimPtr);
			}
		}

		// Since list of selected animation is changed, preview text needs to be change too
		UpdatePreviewText(TargetAnimSequences, PreviewTextWidget);
		return true;
	}

	return false;
}
*/

void FAnimCurveToolModule::UpdateAnimGroupToScale()
{
	AnimSequencesToScale.Reset();
	for (UAnimSequence * Anim : SelectedAnimGroup)
	{
		if (CheckShouldSelectAnim(Anim) &&
			AnimSequencesToScale.Find(Anim) == INDEX_NONE)
		{
			AnimSequencesToScale.Add(Anim);
		}
	}
	UpdatePreviewText(AnimSequencesToScale, AnimSequencesToScalePreview.ToSharedRef());
	
}

void FAnimCurveToolModule::UpdatePreviewText(TArray<UAnimSequence*> & TargetAnimSequences, TSharedRef<STextBlock> PreviewTextWidget)
{
	FString SelectedAnimSequenceNames;
	for (UAnimSequence * Anim : TargetAnimSequences)
	{
		SelectedAnimSequenceNames += Anim->GetName() + "\n";
	}
	if (SelectedAnimSequenceNames.Len() == 0)
		SelectedAnimSequenceNames = "None";
	
	PreviewTextWidget->SetText(FText::FromString(SelectedAnimSequenceNames));
}

FText FAnimCurveToolModule::GetAnimPrefix() const
{
	return AnimPrefix;
}

FText FAnimCurveToolModule::GetAnimPostfix() const
{
	return AnimPostfix;
}

FText FAnimCurveToolModule::GetRateScale() const
{
	return RateScale;
}

FText FAnimCurveToolModule::GetRefTrackName() const
{
	return FText::FromName(RefTrackName);
}


FText FAnimCurveToolModule::GetTolerance() const
{
	return ContactTolerance;
}


void FAnimCurveToolModule::OnAnimPrefixCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	AnimPrefix = InText;
	UpdateAnimGroupToScale();
	//UpdateFilteredAnim(AnimSequencesToScale, AnimSequencesToScalePreview.ToSharedRef(), AnimToScalePath);

}

void FAnimCurveToolModule::OnAnimPostfixCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	AnimPostfix = InText;
	UpdateAnimGroupToScale();

	//UpdateFilteredAnim(AnimSequencesToScale, AnimSequencesToScalePreview.ToSharedRef(), AnimToScalePath);
}

void FAnimCurveToolModule::OnRateScaleCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	RateScale = InText;
}

void FAnimCurveToolModule::OnRefTrackNameCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	const FString str = InText.ToString();
	RefTrackName = FName(*str.TrimStartAndEnd());
}


void FAnimCurveToolModule::OnToleranceCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	ContactTolerance = InText;
}


void FAnimCurveToolModule::OnLeftFootBoneCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	FootLeft = FName(*InText.ToString().TrimStartAndEnd());
}

void FAnimCurveToolModule::OnRightFootBoneCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	FootRight = FName(*InText.ToString().TrimStartAndEnd());
}

FReply FAnimCurveToolModule::ClearReferenceGroup()
{
	AnimReferenceGroup.Reset();
	AnimReferenceGroupPreview->SetText(FText::FromString("None"));
	return FReply::Handled();
}

FText FAnimCurveToolModule::GetRootMotionSpeed() const
{
	return RootMotionSpeed;
}

void FAnimCurveToolModule::OnRootMotionCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
	RootMotionSpeed = InText;
}

TSharedRef<SWidget> FAnimCurveToolModule::MakeSGMarkerWidget()
{
	/*
	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();

	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateRaw(this, &FAnimCurveToolModule::OnAnimToMarkPathSelected);
	PathPickerConfig.DefaultPath = "/Game/";

	TSharedRef<SWidget> SelectedAnimPreview =
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(20, 0, 20, 20)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Animations in Current Directory"))
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(20, 0, 20, 0)
        [
            SAssignNew(AnimSequencesToMarkPreview, STextBlock)
            .Text(FText::FromString("None"))
            .AutoWrapText(true)
        ];
	*/
	
	TSharedRef<SWidget> ReferenceGroupPreview =
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(20, 0, 20, 20)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Animations that have been processed"))
        ]
        + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(20, 0, 20, 0)
        [
            SAssignNew(AnimReferenceGroupPreview, STextBlock)
            .Text(FText::FromString("None"))
            .AutoWrapText(true)
        ];

	// ComboButton + AssetPicker for ref anim sequence
	SAssignNew(SelectRefAnimButtonPtr, SComboButton)
    .OnGetMenuContent_Raw(this, &FAnimCurveToolModule::OnGetRefAnimMenu)
    .ButtonContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString("Choose Anim Sequence"))
    ];
	SAssignNew(SelectRefAnimWidgetPtr, SHorizontalBox)
    + SHorizontalBox::Slot()
    .AutoWidth()
    [
        SelectRefAnimButtonPtr.ToSharedRef()
    ];

	// Thumbnail for Anim Sequence
	RefAnimThumbnailPoolPtr = MakeShared<FAssetThumbnailPool>(10, true);
	RefAnimThumbnailPtr = MakeShareable(new FAssetThumbnail(FAssetData(), 128, 128, RefAnimThumbnailPoolPtr));
	TSharedRef<SWidget> RefAnimThumbnailBox =
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot().MaxWidth(128)
        [
            RefAnimThumbnailPtr->MakeThumbnailWidget()
        ];

	// Editable text widget for reference track name
	TSharedRef<SWidget> RefTrackWidget =
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Enter Ref Track Name"))
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(5, 0, 0, 0).VAlign(VAlign_Center)
        [
            SNew(SEditableTextBox)
            .MinDesiredWidth(50)
            .Text_Raw(this, &FAnimCurveToolModule::GetRefTrackName)
            .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnRefTrackNameCommitted)
        ];

	// Editable text widget for plane height
	/*
	TSharedRef<SWidget> PlaneHeightWidget =
        SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Enter Tolerance"))
        ]
        + SHorizontalBox::Slot().AutoWidth().Padding(5, 0, 0, 0).VAlign(VAlign_Center)
        [
            SNew(SEditableTextBox)
            .MinDesiredWidth(50)
            .Text_Raw(this, &FAnimCurveToolModule::GetTolerance)
            .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnToleranceCommitted)
        ];
	*/
	
	TSharedRef<SWidget> SGMarkerWidget =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(15, 0, 15, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().VAlign(VAlign_Center).Padding(0, 10, 0, 10).MaxHeight(32)
			[
				SNew(STextBlock)	
				.Text(FText::FromString("Automark Animations"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
			]
			/*
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 20)
        	[
        	    ContentBrowser.CreatePathPicker(PathPickerConfig)
        	]
        	+ SVerticalBox::Slot().AutoHeight()
            [
				SelectedAnimPreview		
            ]
            */
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 5).VAlign(VAlign_Center)
    		[
    		    SNew(STextBlock)
    		    .Text(FText::FromString("Enter Left Foot BoneName"))
    		]
    		+ SVerticalBox::Slot().AutoHeight().Padding(5, 0, 0, 0).VAlign(VAlign_Center)
    		[
    		    SNew(SEditableTextBox)
    		    .MinDesiredWidth(50)
    		    .Text_Raw(this, &FAnimCurveToolModule::GetLeftFootBone)
    		    .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnLeftFootBoneCommitted)
    		]
    		+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 5).VAlign(VAlign_Center)
    		.VAlign(VAlign_Center)
    		[
    		    SNew(STextBlock)
    		    .Text(FText::FromString("Enter Right Foot BoneName"))
    		]
    		+ SVerticalBox::Slot().AutoHeight().Padding(5, 0, 0, 0).VAlign(VAlign_Center)
    		[
    		    SNew(SEditableTextBox)
    		    .MinDesiredWidth(50)
    		    .Text_Raw(this, &FAnimCurveToolModule::GetRightFootBone)
    		    .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnRightFootBoneCommitted)
    		]
    		+ SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 5).VAlign(VAlign_Center)
            [
                SNew(SButton)
                .OnClicked_Raw(this, &FAnimCurveToolModule::AddAllReferenceGroup)
                .Text(FText::FromString("Precalculate"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 5).VAlign(VAlign_Center)
            [
                SNew(SButton)
                .OnClicked_Raw(this, &FAnimCurveToolModule::ClearReferenceGroup)
                .Text(FText::FromString("Clear Reference Group"))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 5).VAlign(VAlign_Center)
            [
                ReferenceGroupPreview	
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 5).VAlign(VAlign_Center)
            [
                SNew(SButton)
                .OnClicked_Raw(this, &FAnimCurveToolModule::AddDefaultMarkerForReferenceGroup)
                .Text(FText::FromString("Add Default Markers to ReferenceGroup"))
            ]
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(15, 0, 15, 0)
		[
			SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().VAlign(VAlign_Center).Padding(0, 20, 0, 20)
			[
				SNew(STextBlock)
				.Text(FText::FromString("Select Ref Animation"))
			]
			+SVerticalBox::Slot().MaxHeight(30).Padding(0, 20, 0, 20)
			[
				SelectRefAnimWidgetPtr.ToSharedRef()
			]
			+ SVerticalBox::Slot().MaxHeight(128).Padding(0, 20, 0, 20)
			[
				RefAnimThumbnailBox
			]
			+ SVerticalBox::Slot().MaxHeight(128).Padding(0, 20, 0, 20)
            [
                RefTrackWidget
            ]
            +SVerticalBox::Slot().MaxHeight(24).Padding(0, 20, 0, 20)
            [
				SNew(SButton)
				.Text(FText::FromString("Sync Reference Group"))
				.OnClicked_Raw(this, &FAnimCurveToolModule::SyncReferenceGroupOnClicked)
            ]
		];
	return SGMarkerWidget;
}

void FAnimCurveToolModule::OnRefAnimAssetSelected(const FAssetData& AssetData)
{
	RefAnimSeuquence = Cast<UAnimSequence>(AssetData.GetAsset());
	SelectRefAnimButtonPtr->SetIsOpen(false);
	TSharedRef<SWidget> NewButton =
        SNew(SComboButton)
            .OnGetMenuContent_Raw(this, &FAnimCurveToolModule::OnGetRefAnimMenu)
            .ButtonContent()
            [
                SNew(STextBlock)
                .Text(FText::FromName(AssetData.AssetName))
            ];
	SelectRefAnimButtonPtr->SetContent(NewButton);
	RefAnimThumbnailPtr->SetAsset(AssetData);
	RefAnimThumbnailPtr->RefreshThumbnail();
}

TSharedRef<SWidget> FAnimCurveToolModule::OnGetRefAnimMenu()
{
	TArray<const UClass*> ClassFilters;
	ClassFilters.Add(UAnimSequence::StaticClass());
	FAssetData CurrentAssetData = FAssetData();
	
	TSharedPtr<SWidget> Widget = PropertyCustomizationHelpers::MakeAssetPickerWithMenu(
    FAssetData(),
    true,
    ClassFilters,
    TArray<UFactory*>(),
    FOnShouldFilterAsset::CreateLambda([CurrentAssetData](const FAssetData& InAssetData) { return InAssetData == CurrentAssetData; }),
    FOnAssetSelected::CreateRaw(this, &FAnimCurveToolModule::OnRefAnimAssetSelected),
    FSimpleDelegate()
    );

	return Widget.ToSharedRef();
}

FReply FAnimCurveToolModule::AddAllReferenceGroup()
{
	AddToReferenceGroup(SelectedAnimGroup, AnimReferenceGroup);
	return FReply::Handled();
}

void FAnimCurveToolModule::AddToReferenceGroup(TArray<UAnimSequence*>& AnimSequences, TMap<UAnimSequence*, SGMarkerReference>& ReferenceGroup)
{
	for (UAnimSequence* Anim : AnimSequences)
	{
	
		if (ReferenceGroup.Find(Anim) == nullptr)
		{
			if (Anim->GetSkeleton()->GetReferenceSkeleton().FindRawBoneIndex(FootLeft) == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("Bone %s not found in animation %s"), *FootLeft.ToString(), *Anim->GetName());
				continue;
			}

			if (Anim->GetSkeleton()->GetReferenceSkeleton().FindRawBoneIndex(FootRight) == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("Bone %s not found in animation %s"), *FootRight.ToString(), *Anim->GetName());
				continue;
			}
			SGMarkerReference ref = SGMarkerReference(Anim, FootLeft, FootRight);
			if (ref.bIsValid)
			{
				ReferenceGroup.Add(Anim, ref);
			}
		}
	}

	TArray<UAnimSequence*> PreviewAnimSequence;
	for (auto e : ReferenceGroup)
	{
		PreviewAnimSequence.Add(e.Key);
	}

	UpdatePreviewText(PreviewAnimSequence, AnimReferenceGroupPreview.ToSharedRef());
}

FReply FAnimCurveToolModule::SyncReferenceGroupOnClicked()
{
	SyncReferenceGroup(RefAnimSeuquence, RefTrackName);

	return FReply::Handled();
}

void FAnimCurveToolModule::SyncReferenceGroup(UAnimSequence* RefAnimSequence, FName TrackName)
{
	// Sanity Check
	if (AnimReferenceGroup.Find(RefAnimSequence) == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Animation %s doesn't belong to the current reference group being processed."), *RefAnimSequence->GetName());
		return;
	}
	const bool bExistingTrackName = GetTrackIndexForAnimationNotifyTrackName(RefAnimSequence, TrackName) != INDEX_NONE;
	if (!bExistingTrackName)
	{
		UE_LOG(LogTemp, Warning, TEXT("Track %s not found in animation %s."), *TrackName.ToString(), *RefAnimSequence->GetName());
		return;
	}
	const int32 TrackIndex = GetTrackIndexForAnimationNotifyTrackName(RefAnimSequence, TrackName);
	TArray<FAnimSyncMarker> AllMarkers;
	TArray<FAnimNotifyEvent> AllNotifies;

	
	// 记录所有的同步标记
	for (FAnimSyncMarker & m : RefAnimSequence->AuthoredSyncMarkers)
	{
		if (m.TrackIndex == TrackIndex)
		{
			AllMarkers.Add(m);
		}
	}
	
	// 记录所有的通知
	for (FAnimNotifyEvent & e : RefAnimSequence->Notifies)
	{
		if (e.TrackIndex == TrackIndex)
		{
			AllNotifies.Add(e);
		}
	}
	
	
	// 将记录下的通知与标记同步
	// 对参考动画来说，也可能获得新的标记与通知，因为它可以包含不止一个循环
	for (auto & Anim : AnimReferenceGroup)
	{
		// 移除现存同名轨道上的所有通知与同步标记
		Anim.Key->Notifies.RemoveAll([&](const FAnimNotifyEvent& Notify) { return Notify.TrackIndex == TrackIndex; });
		Anim.Key->AuthoredSyncMarkers.RemoveAll([&](const FAnimSyncMarker& Marker) { return Marker.TrackIndex == TrackIndex; });

		Anim.Key->RefreshCacheData();
		
		float RefRatio;
		bool bOrderIsLeftRight;
		for(FAnimSyncMarker & m : AllMarkers)
		{
			
			// 将时间换算为标记区间上的比例（唯一值）
			AnimReferenceGroup[RefAnimSequence].GetRatioFromTime(m.Time, RefRatio, bOrderIsLeftRight);

			// 将比例换算为时间，当动画为多循环时，synctime会有多个元素
			TArray<float> SyncTime;
			AnimReferenceGroup[Anim.Key].GetTimeFromRatio(RefRatio, bOrderIsLeftRight, SyncTime);
			for(float & Time : SyncTime)
			{
				// 避免向重复的时间添加标记
				bool ExistedAtTime = false;
				for(auto s : Anim.Key->AuthoredSyncMarkers)
				{
					if (s.TrackIndex == TrackIndex)
						ExistedAtTime |= FMath::IsNearlyEqual(s.Time, Time, 0.01f);
				}
				if (!ExistedAtTime)
					AddContactMarker(Anim.Key, TrackName, m.MarkerName, Time);
			}
		}
		for(FAnimNotifyEvent & e : AllNotifies)
		{
			// 将时间换算为标记区间上的比例（唯一值）
			AnimReferenceGroup[RefAnimSequence].GetRatioFromTime(e.GetTime(), RefRatio, bOrderIsLeftRight);
			
			// 将比例换算为时间，当动画为多循环时，synctime会有多个元素
			TArray<float> SyncTime;
			AnimReferenceGroup[Anim.Key].GetTimeFromRatio(RefRatio, bOrderIsLeftRight, SyncTime);
			for(float & Time : SyncTime)
			{
				// 避免向重复的时间添加标记
				bool ExistedAtTime = false;
				for(auto n : Anim.Key->Notifies)
				{
					if (n.TrackIndex == TrackIndex)
						ExistedAtTime |= FMath::IsNearlyEqual(n.GetTime(), Time, 0.01f);
				}
				if (!ExistedAtTime)
					AddAnimationNotifyEvent(Anim.Key, TrackName, Time, e.Notify->GetClass());
			}
        }
		Anim.Key->RefreshCacheData();

	}

	return;
}

/*
void FAnimCurveToolModule::OnAnimToMarkPathSelected(const FString& NewPath)
{
	AnimToMarkPath = NewPath;
	UpdateFilteredAnim(AutoMarkAnimGroup, AnimSequencesToMarkPreview.ToSharedRef(), AnimToMarkPath);
}
*/

FReply FAnimCurveToolModule::AddDefaultMarkerForReferenceGroup()
{
	FName TrackName = FName(TEXT("Default Track"));
	for (auto & e : AnimReferenceGroup)
	{
		RemoveAnimationNotifyTrack(e.Key, TrackName);
		for (auto l : e.Value.LeftMarkers)
		{
			AddContactMarker(e.Value.AnimSequence, TrackName, FName(TEXT("Marker_l")), l);
		}

		for (auto r : e.Value.RightMarkers)
		{
			AddContactMarker(e.Value.AnimSequence, TrackName, FName(TEXT("Marker_r")), r);
		}
	}
	return FReply::Handled();
}

void FAnimCurveToolModule::AddContactMarker(UAnimSequence * AnimSequence, FName TrackName, FName MarkerName, float MarkerTime)
{
	const bool bExistingTrackName = GetTrackIndexForAnimationNotifyTrackName(AnimSequence, TrackName) != INDEX_NONE;

	// Remove existing markers on the sync track if there is an old track
	if (bExistingTrackName)
	{
		const int32 TrackIndex = GetTrackIndexForAnimationNotifyTrackName(AnimSequence, TrackName);
		if (TrackIndex != INDEX_NONE)
		{
			AnimSequence->RefreshSyncMarkerDataFromAuthored();

			// Refresh all cached data
			AnimSequence->RefreshCacheData();
		}
	}
	else
	{
		AddAnimationNotifyTrack(AnimSequence, TrackName, FLinearColor::White);
	}

	AddAnimationSyncMarker(AnimSequence, MarkerName, MarkerTime, TrackName);
}

FTransform FAnimCurveToolModule::GetBoneTMRelativeToRoot(UAnimSequence* AnimationSequence, FName BoneName, int Frame)
{
	FTransform Transform = FTransform::Identity;
	TArray<FName> BonePath;
	FindBonePathToRoot(AnimationSequence, BoneName, BonePath);
	for (FName CurBone : BonePath)
	{
		int32 BoneIndex = AnimationSequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(CurBone);
		if (BoneIndex == INDEX_NONE)
			continue;
		
		int32 TrackIndex = GetAnimTrackIndexForSkeletonBone(BoneIndex, AnimationSequence->GetRawTrackToSkeletonMapTable());
		FTransform BoneTransform;

		AnimationSequence->GetBoneTransform(BoneTransform, TrackIndex, AnimationSequence->GetTimeAtFrame(Frame), false);

		if (BoneIndex == 0)
		{
			BoneTransform.SetLocation(FVector(0, 0, 0));	
		}
		
		//UE_LOG(LogTemp, Warning, TEXT("%s %d: %s"), *CurBone.ToString(), TrackIndex, *BoneTransform.ToString());
		Transform = Transform * BoneTransform;
	}
	return Transform;
}

void FAnimCurveToolModule::AddAnimationSyncMarker(UAnimSequence* AnimationSequence, FName MarkerName, float Time, FName TrackName)
{
	if (AnimationSequence)
	{
		const bool bIsValidTime = FMath::IsWithinInclusive(Time, 0.0f, AnimationSequence->SequenceLength);

		if (bIsValidTime)
		{
			FAnimSyncMarker NewMarker;
			NewMarker.MarkerName = MarkerName;
			NewMarker.Time = Time;
			NewMarker.TrackIndex = GetTrackIndexForAnimationNotifyTrackName(AnimationSequence, TrackName);

			AnimationSequence->AuthoredSyncMarkers.Add(NewMarker);
			AnimationSequence->AnimNotifyTracks[NewMarker.TrackIndex].SyncMarkers.Add(&AnimationSequence->AuthoredSyncMarkers.Last());
						
			AnimationSequence->RefreshSyncMarkerDataFromAuthored();

			// Refresh all cached data
			AnimationSequence->RefreshCacheData();
		}
	}
}

UAnimNotify* FAnimCurveToolModule::AddAnimationNotifyEvent(UAnimSequence* AnimationSequence, FName NotifyTrackName, float StartTime, TSubclassOf<UAnimNotify> NotifyClass)
{
	UAnimNotify* Notify = nullptr;
	if (AnimationSequence)
	{
		const bool bIsValidTrackName = GetTrackIndexForAnimationNotifyTrackName(AnimationSequence, NotifyTrackName) != INDEX_NONE;
		const bool bIsValidTime = FMath::IsWithinInclusive(StartTime, 0.0f, AnimationSequence->SequenceLength);

		if (bIsValidTrackName && bIsValidTime)
		{
			FAnimNotifyEvent& NewEvent = AnimationSequence->Notifies.AddDefaulted_GetRef();

			NewEvent.NotifyName = NAME_None;
			NewEvent.Link(AnimationSequence, StartTime);
			NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(AnimationSequence->CalculateOffsetForNotify(StartTime));
			NewEvent.TrackIndex = GetTrackIndexForAnimationNotifyTrackName(AnimationSequence, NotifyTrackName);
			NewEvent.NotifyStateClass = nullptr;

			if (NotifyClass)
			{
				Notify = NewObject<UAnimNotify>(AnimationSequence, NotifyClass, NAME_None, RF_Transactional);
				NewEvent.Notify = Notify;

				// Setup name for new event
				if(NewEvent.Notify)
				{
					NewEvent.NotifyName = FName(*NewEvent.Notify->GetNotifyName());
				}
			}
			else
			{
				NewEvent.Notify = nullptr;
			}

			// Refresh all cached data
			AnimationSequence->RefreshCacheData();
		}
	}

	return Notify;
}

void FAnimCurveToolModule::AddAnimationNotifyTrack(UAnimSequence* AnimationSequence, FName NotifyTrackName,
	FLinearColor TrackColor)
{
	if (AnimationSequence)
	{
		const bool bExistingTrackName = GetTrackIndexForAnimationNotifyTrackName(AnimationSequence, NotifyTrackName) != INDEX_NONE;
		if (bExistingTrackName)
		{
			// Remove Old Sync Track
			RemoveAnimationNotifyTrack(AnimationSequence, NotifyTrackName);
		}
		FAnimNotifyTrack NewTrack;
		NewTrack.TrackName = NotifyTrackName;
		NewTrack.TrackColor = TrackColor;
		AnimationSequence->AnimNotifyTracks.Add(NewTrack);
  
		// Refresh all cached data
		AnimationSequence->RefreshCacheData();
	}
}

void FAnimCurveToolModule::RemoveAnimationNotifyTrack(UAnimSequence* AnimationSequence, FName NotifyTrackName)
{
	if (AnimationSequence)
	{
		const int32 TrackIndexToDelete = GetTrackIndexForAnimationNotifyTrackName(AnimationSequence, NotifyTrackName);
		if (TrackIndexToDelete != INDEX_NONE)
		{	
			// Remove all notifies and sync markers on the to-delete-track
			AnimationSequence->Notifies.RemoveAll([&](const FAnimNotifyEvent& Notify) { return Notify.TrackIndex == TrackIndexToDelete; });
			AnimationSequence->AuthoredSyncMarkers.RemoveAll([&](const FAnimSyncMarker& Marker) { return Marker.TrackIndex == TrackIndexToDelete; });

			// Before track removal, make sure everything behind is fixed
			for (FAnimNotifyEvent& Notify : AnimationSequence->Notifies)
			{
				if (Notify.TrackIndex > TrackIndexToDelete)
				{
					Notify.TrackIndex = Notify.TrackIndex - 1;
				}				
			}
			for (FAnimSyncMarker& SyncMarker : AnimationSequence->AuthoredSyncMarkers)
			{
				if (SyncMarker.TrackIndex > TrackIndexToDelete)
				{
					SyncMarker.TrackIndex = SyncMarker.TrackIndex - 1;
				}
			}
			
			// Delete the track itself
			AnimationSequence->AnimNotifyTracks.RemoveAt(TrackIndexToDelete);

			// Refresh all cached data
			AnimationSequence->RefreshCacheData();
		}		
	}
}

int32 FAnimCurveToolModule::GetTrackIndexForAnimationNotifyTrackName(const UAnimSequence* AnimationSequence, FName NotifyTrackName)
{
	return AnimationSequence->AnimNotifyTracks.IndexOfByPredicate(
           [&](const FAnimNotifyTrack& Track)
       {
           return Track.TrackName == NotifyTrackName;
       });
}

void FAnimCurveToolModule::FindBonePathToRoot(const UAnimSequence* AnimationSequence, FName BoneName, TArray<FName>& BonePath)
{
	BonePath.Empty();
	if (AnimationSequence)
	{
		BonePath.Add(BoneName);
		int32 BoneIndex = AnimationSequence->GetSkeleton()->GetReferenceSkeleton().FindRawBoneIndex(BoneName);		
		if (BoneIndex != INDEX_NONE)
		{
			while (BoneIndex != INDEX_NONE)
			{
				const int32 ParentBoneIndex = AnimationSequence->GetSkeleton()->GetReferenceSkeleton().GetRawParentIndex(BoneIndex);
				if (ParentBoneIndex != INDEX_NONE)
				{
					BonePath.Add(AnimationSequence->GetSkeleton()->GetReferenceSkeleton().GetBoneName(ParentBoneIndex));
					//UE_LOG(LogTemp, Warning, TEXT("%s"), *AnimationSequence->GetSkeleton()->GetReferenceSkeleton().GetBoneName(ParentBoneIndex).ToString())
				}

				BoneIndex = ParentBoneIndex;
			}
		}
	}
}

int32 FAnimCurveToolModule::GetAnimTrackIndexForSkeletonBone(const int32 InSkeletonBoneIndex,
	const TArray<FTrackToSkeletonMap>& TrackToSkelMap)
{
	return TrackToSkelMap.IndexOfByPredicate([&](const FTrackToSkeletonMap& TrackToSkel)
    { 
        return TrackToSkel.BoneTreeIndex == InSkeletonBoneIndex;
    });
}


/******* Deprecated Functions for Curve Extractor *************/
/*
TSharedRef<SWidget> FAnimCurveToolModule::MakeCurveExtractor()
{
	// ComboButton + AssetPicker for anim sequence
    SAssignNew(SelectAnimSequenceButtonPtr, SComboButton)
    .OnGetMenuContent_Raw(this, &FAnimCurveToolModule::OnGetAnimMenu)
    .ButtonContent()
    [
        SNew(STextBlock)
        .Text(FText::FromString("Choose Anim Sequence"))
    ];
    SAssignNew(SelectAnimSequenceWidgetPtr, SHorizontalBox)
    + SHorizontalBox::Slot()
    .AutoWidth()
	[
		SelectAnimSequenceButtonPtr.ToSharedRef()
    ];

	// Widget that allow scaling of the exported curve
	TSharedRef<SWidget> ScalingFactorWidget =
        SNew(SVerticalBox)
        + SVerticalBox::Slot().Padding(0, 0, 0, 5)
        [
			
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString("Translation Scale"))
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SEditableTextBox)
                .MinDesiredWidth(50)
                .Text_Raw(this, &FAnimCurveToolModule::GetTransScale)
                .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnTransScaleCommitted)
            ]
        ]
        + SVerticalBox::Slot()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString("Rotation Scale"))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(5, 0, 0, 0)
            .VAlign(VAlign_Center)
            [
                SNew(SEditableTextBox)
                .MinDesiredWidth(50)
                .Text_Raw(this, &FAnimCurveToolModule::GetRotScale)
                .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnRotScaleCommitted)
            ]
        ];
	
	// Enter and Display bone name, Button for extracting curve
	TSharedRef<SWidget> BoneNameWidget =
    	SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        [
            SNew(STextBlock)
            .Text(FText::FromString("Enter Bone Name"))
        ]
    	+ SHorizontalBox::Slot().AutoWidth().Padding(5, 0, 0, 0).VAlign(VAlign_Center)
    	[
    	    SNew(SEditableTextBox)
    	    .MinDesiredWidth(50)
    	    .Text_Raw(this, &FAnimCurveToolModule::GetBoneName)
    	    .OnTextCommitted_Raw(this, &FAnimCurveToolModule::OnBoneNameCommitted)
    	];

	TSharedRef<SWidget> ExtractCurveButton =
		SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(FText::FromString("Extract Curve"))
			.OnClicked_Raw(this, &FAnimCurveToolModule::ExtractBoneCurve)
		];
		
	
	// Thumbnail for Anim Sequence
	AnimThumbnailPoolPtr = MakeShared<FAssetThumbnailPool>(10, true);
	AnimThumbnailPtr = MakeShareable(new FAssetThumbnail(FAssetData(), 128, 128, AnimThumbnailPoolPtr));
	TSharedRef<SWidget> AnimThumbnailBox =
    	SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().MaxWidth(128)
		[
			AnimThumbnailPtr->MakeThumbnailWidget()
		];

	// A matrix of checkboxes that allow output selection
	TSharedRef<SWidget> OutputBoxWidget =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString("Output Selection"))
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeOutputCheckboxRow("Translation")
		]
		+ SVerticalBox::Slot().AutoHeight()
        [
			MakeOutputCheckboxRow("Rotation")
        ]
		+ SVerticalBox::Slot().AutoHeight()
        [
			MakeOutputCheckboxRow("Scale")
        ];

	// Add widget to edit where the curves are save
	IContentBrowserSingleton& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get();
	FPathPickerConfig PathPickerConfig;
	PathPickerConfig.OnPathSelected = FOnPathSelected::CreateRaw(this, &FAnimCurveToolModule::OnSavePathSelected);
	PathPickerConfig.DefaultPath = "/Game/";

	TSharedRef<SWidget> SavePathWidget =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString("Output Path"))
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 5)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().MaxWidth(200)
			[
				ContentBrowser.CreatePathPicker(PathPickerConfig)
			]
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			ExtractCurveButton
		];
	
	TSharedRef<SWidget> Extractor =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().VAlign(VAlign_Center).Padding(15, 10, 15, 20)
    	  .MaxHeight(32)
    	  [
    	      SNew(STextBlock)
    	      .Text(FText::FromString("Extract AnimSequence Curve"))
    	      .Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
    	  ]
          + SVerticalBox::Slot().VAlign(VAlign_Center).Padding(15, 0, 15, 5).MaxHeight(32)
          [
              SelectAnimSequenceWidgetPtr.ToSharedRef()
          ]
          + SVerticalBox::Slot()
          .AutoHeight().MaxHeight(128).VAlign(VAlign_Center).Padding(15, 0, 15, 5)
          [
              AnimThumbnailBox
          ]
          + SVerticalBox::Slot().VAlign(VAlign_Center).Padding(15, 0, 15, 5)
          .MaxHeight(32)
          [
              BoneNameWidget
          ]
          +SVerticalBox::Slot().VAlign(VAlign_Center).Padding(15, 0, 15, 5)
        .MaxHeight(64)
        [
            ScalingFactorWidget
        ]
        +SVerticalBox::Slot().VAlign(VAlign_Center).Padding(15, 0, 15, 5)
        .MaxHeight(128)
        [
            OutputBoxWidget
        ]
        +SVerticalBox::Slot().VAlign(VAlign_Center).Padding(15, 0, 15, 20)
          .MaxHeight(512)
        [
            SavePathWidget
        ];

	return Extractor;
}

void FAnimCurveToolModule::OnAnimAssetSelected(const FAssetData& AssetData)
{
	AnimSequenceToExtract = Cast<UAnimSequence>(AssetData.GetAsset());
	SelectAnimSequenceButtonPtr->SetIsOpen(false);
	TSharedRef<SWidget> NewButton =
		SNew(SComboButton)
            .OnGetMenuContent_Raw(this, &FAnimCurveToolModule::OnGetAnimMenu)
            .ButtonContent()
            [
                SNew(STextBlock)
                .Text(FText::FromName(AssetData.AssetName))
            ];
	SelectAnimSequenceButtonPtr->SetContent(NewButton);
	AnimThumbnailPtr->SetAsset(AssetData);
	AnimThumbnailPtr->RefreshThumbnail();
}

TSharedRef<SWidget> FAnimCurveToolModule::OnGetAnimMenu()
{
	TArray<const UClass*> ClassFilters;
	ClassFilters.Add(UAnimSequence::StaticClass());
	FAssetData CurrentAssetData = FAssetData();
	
	AnimSequencePicker = PropertyCustomizationHelpers::MakeAssetPickerWithMenu(
    FAssetData(),
    true,
    ClassFilters,
    TArray<UFactory*>(),
    FOnShouldFilterAsset::CreateLambda([CurrentAssetData](const FAssetData& InAssetData) { return InAssetData == CurrentAssetData; }),
    FOnAssetSelected::CreateRaw(this, &FAnimCurveToolModule::OnAnimAssetSelected),
    FSimpleDelegate()
    );

	return AnimSequencePicker.ToSharedRef();
}

bool FAnimCurveToolModule::FillCurveVector() 
{
	if(!AnimSequenceToExtract)
		return false;
	const int BoneIndex = AnimSequenceToExtract->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(TargetBoneName);
	const int TrackIndex = GetAnimTrackIndexForSkeletonBone(BoneIndex, AnimSequenceToExtract->GetRawTrackToSkeletonMapTable());
	if (BoneIndex == INDEX_NONE)
		return false;

	TransCurve.Reset();
	RotCurve.Reset();
	ScaleCurve.Reset();
	
	const float TScale = FCString::Atof(*TransScale.ToString());
	const float RScale = FCString::Atof(*RotScale.ToString());
	const float DeltaTime = AnimSequenceToExtract->GetPlayLength() / AnimSequenceToExtract->GetNumberOfFrames();
	for (int i = 0; i < AnimSequenceToExtract->GetNumberOfFrames(); i++)
	{
		FTransform Out;
		AnimSequenceToExtract->GetBoneTransform(Out, TrackIndex , i * DeltaTime, true);
		TransCurve.Add(Out.GetLocation() * TScale);
		RotCurve.Add(Out.GetRotation().Euler() * RScale);
		ScaleCurve.Add(Out.GetScale3D());
	}
	return true;
}

void FAnimCurveToolModule::SaveCurve()
{
	for (const TPair<FString, bool> & pair : OutputBoxStateMap)
	{
		if (pair.Value)
		{
			FString str = pair.Key;
			SaveCurveFloat(str.LeftChop(1), str.RightChop(str.Len()-1));
		}
	}
}

TSharedRef<SWidget> FAnimCurveToolModule::MakeOutputCheckbox(FString Type, FString Axis)
{
	return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .MaxHeight(24)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Axis))
            ]
            + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .IsChecked_Raw(this, &FAnimCurveToolModule::IsOutputBoxChecked, Type, Axis)
				.OnCheckStateChanged_Raw(this, &FAnimCurveToolModule::OnOutputCheckboxChanged, Type, Axis)
            ]
        ];
}

ECheckBoxState FAnimCurveToolModule::IsOutputBoxChecked(FString Type, FString Axis) const
{
	return (OutputBoxStateMap[Type+Axis] ? ECheckBoxState::Checked : ECheckBoxState::Unchecked);
}

void FAnimCurveToolModule::OnOutputCheckboxChanged(ECheckBoxState NewState, FString Type, FString Axis)
{
	if (Axis != "All")
	{
		// When a new box is checked or unchecked, make sure to update "All Box"
		if (NewState == ECheckBoxState::Checked)
		{
			OutputBoxStateMap[Type+Axis] = true;
			OutputBoxStateMap[Type+"All"] = OutputBoxStateMap[Type+"X"] &&
											OutputBoxStateMap[Type+"Y"] &&
											OutputBoxStateMap[Type+"Z"];	
		}
		else
		{
			OutputBoxStateMap[Type+Axis] = false;
			OutputBoxStateMap[Type+"All"] = false;	
		}
	}
	else
	{
		// When the "All Box" is checked, set all the other boxes
		if (NewState == ECheckBoxState::Checked)
		{
			OutputBoxStateMap[Type+"All"] = true;
			OutputBoxStateMap[Type+"X"] = true;
			OutputBoxStateMap[Type+"Y"] = true;
			OutputBoxStateMap[Type+"Z"] = true;
		}
		else
		{
			OutputBoxStateMap[Type+"All"] = false;
			OutputBoxStateMap[Type+"X"] = false;
			OutputBoxStateMap[Type+"Y"] = false;
			OutputBoxStateMap[Type+"Z"] = false;
		}
	}
}

void FAnimCurveToolModule::SaveCurveFloat(FString Type, FString Axis) const
{
	if (Axis == "All")
		return;
	
	TArray<FVector> TargetCurve;
	int TargetAxis;
	if (Type == "Translation")
	{
		TargetCurve = TransCurve;
	}
	else if (Type == "Rotation")
	{
		TargetCurve = RotCurve;
	}
	else if (Type == "Scale")
	{
		TargetCurve = ScaleCurve;
	}
	
	if (TargetCurve.Num() == 0)
		return;
	
	if (Axis == "X")
	{
		TargetAxis = 0;
	}
	else if (Axis == "Y")
	{
		TargetAxis = 1;
	}
	else
	{
		TargetAxis = 2;
	}

	// Organize Package
	FString PackageName = SavePath;
	// It seems that without the "/XXX" in the end, the file can't be saved in the corresponding folder correctly
	const FString AnimName = AnimSequenceToExtract->GetName();
	const FString BoneID = TargetBoneName.ToString();
	const FString CurveName = AnimName + "_" + BoneID + "_" + Type + Axis;
	PackageName += "/" + AnimName + "/" + CurveName;
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UCurveFloat* Curve = NewObject<UCurveFloat>(Package, *(CurveName), RF_Public | RF_Standalone | RF_MarkAsRootSet);
	const float DeltaTime = AnimSequenceToExtract->GetPlayLength() / AnimSequenceToExtract->GetNumberOfFrames();

	// Actually copy the data into the asset
	for (int i = 0; i < AnimSequenceToExtract->GetNumberOfFrames(); i++)
	{
		Curve->FloatCurve.AddKey(i * DeltaTime, TargetCurve[i][TargetAxis]);
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Curve);
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
	UPackage::SavePackage(Package, Curve, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *PackageFileName, GError, nullptr, true, true, SAVE_NoError);
}

TSharedRef<SWidget> FAnimCurveToolModule::MakeOutputCheckboxRow(FString Type)
{
	// Add Padding so the checkbox rows are aligned
	int Padding = 0;
	if (Type == "Rotation")
	{
		Padding = 15;
	}
	else if (Type == "Scale")
	{
		Padding = 33;
	}

	return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5 + Padding, 0)
            [
                SNew(STextBlock)
                .Text(FText::FromString(Type))
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
            [
                MakeOutputCheckbox(Type, "X")
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
            [
                MakeOutputCheckbox(Type, "Y")
            ]
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 5, 0)
            [
                MakeOutputCheckbox(Type, "Z")
            ]
			+ SHorizontalBox::Slot().AutoWidth()
            [
                MakeOutputCheckbox(Type, "All")
            ];
}



void FAnimCurveToolModule::OnBoneNameCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
const FString str = InText.ToString();
TargetBoneName = FName(*str.TrimStartAndEnd());
}

void FAnimCurveToolModule::OnTransScaleCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
TransScale = InText;
}

void FAnimCurveToolModule::OnRotScaleCommitted(const FText& InText, ETextCommit::Type CommitInfo)
{
RotScale = InText;
}




FReply FAnimCurveToolModule::ExtractBoneCurve()
{
if (FillCurveVector())
SaveCurve();
return FReply::Handled();
}

FText FAnimCurveToolModule::GetBoneName() const
{
return FText::FromName(TargetBoneName);
}

FText FAnimCurveToolModule::GetTransScale() const
{
return TransScale;
}

FText FAnimCurveToolModule::GetRotScale() const
{
return RotScale;
}
*/

/******* 不同同步组计算方案的废弃函数 *************/
/*
TArray<float> SGMarkerReference::GetContactTime(UAnimSequence* AnimationSequence, FName BoneName, float Threshold)
{
	UE_LOG(LogTemp, Warning, TEXT("%s"), *BoneName.ToString());
	
	TArray<FName> BonePath;
	float MarkerTime = 0;
	float DeltaTime = AnimationSequence->GetPlayLength() / AnimationSequence->GetNumberOfFrames();
	int LastFrame = AnimationSequence->GetNumberOfFrames() - 1;
	FTransform Transform;
	float MinHeight = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, LastFrame).GetLocation().Z;
	float MinHeightTime = 0;

	// 第一次遍历，找到最低点
	for (int i = 0; i < AnimationSequence->GetNumberOfFrames(); i++)
	{
		Transform = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, i);
		//UE_LOG(LogTemp, Warning, TEXT("%s"), *Transform.ToString());
		if (Transform.GetLocation().Z < MinHeight)
		{
			MinHeight = Transform.GetLocation().Z;
		}
	}

	Threshold = MinHeight + Tolerance;
	UE_LOG(LogTemp, Warning, TEXT("%s Threshold: %f"), *AnimationSequence->GetName() ,Threshold);

	int StartCount = 0;
	// 如果动画的结束帧处在threshold以下，找到这一触地区间的开始点的前一帧
	if (FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, LastFrame).GetLocation().Z < Threshold)
	{
		FTransform StartTrans;
		StartCount = AnimationSequence->GetNumberOfFrames();
		do
		{
			StartCount --;
			StartTrans = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, StartCount);
			//UE_LOG(LogTemp, Warning, TEXT("%s"), *StartTrans.ToString());
		}
		while(StartCount >= 0 && StartTrans.GetLocation().Z < Threshold);
	}
	
	// 第二次遍历，用滑动窗口计算触地过程中的总位移，并结合动画方向判断该触地区间是否有效
	float CumulativeDist = 0;
	int Contact;

	TArray<float> Result;
	for (int i = 0; i < AnimationSequence->GetNumberOfFrames(); i++)
	{
		int index = (i + StartCount) % AnimationSequence->GetNumberOfFrames();
		// 触地中，计算累积位移
		if (FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BoneName, index).GetLocation().Z < Threshold)
		{
			if (CumulativeDist == 0)
			{
				Contact = index;
			}
			CumulativeDist += GetDisplacementAtFrame(index, BoneName);
			UE_LOG(LogTemp, Warning, TEXT("%d Distance: %f"), i, CumulativeDist);
		}
		// 当区间结束时，判断累计位移是否与动画移动方向吻合
		else
		{
			if (IsCumulativeDistanceValid(CumulativeDist))
			{
				Result.Add(AnimSequence->GetTimeAtFrame(Contact));
				UE_LOG(LogTemp, Warning, TEXT("Valid Contact found: Start: %d  End: %d  Distance: %f"), Contact, index, CumulativeDist);
			}
			CumulativeDist = 0;
			Contact = index;
		}
	}

	if (Result.Num() == 0)
		UE_LOG(LogTemp, Warning, TEXT("No Contact Point Found for %s %s"), *AnimationSequence->GetName(), *BoneName.ToString());
	return Result;
}

float SGMarkerReference::GetDisplacementAtFrame(int frame, FName BoneName)
{
	int LastFrame = frame - 1;
	if (frame == 0)
	{
		LastFrame = AnimSequence->GetNumberOfFrames()-1;
	}

	const FVector v1 = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimSequence, BoneName, frame).GetLocation();
	//UE_LOG(LogTemp, Warning, TEXT("%s"), *v1.ToString());
	const FVector v2 = FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimSequence, BoneName, LastFrame).GetLocation();

	// 计算位移
	if(Dir == Direction::r || Dir == Direction::l)
	{
		return v1.X - v2.X;
	}
	else
	{
		return v1.Y - v2.Y;
	}
}

bool SGMarkerReference::IsCumulativeDistanceValid(float Distance)
{
	// 积累位移需要与移动方向相反
	if (Dir == Direction::f || Dir == Direction::rf || Dir == Direction::lf)
	{
		return Distance < 0;
	}
	else if (Dir == Direction::b || Dir == Direction::rb || Dir == Direction::lb)
	{
		return Distance > 0;
	}
	else if (Dir == Direction::r)
	{
		return Distance > 0;
	}
	else
	{
		return Distance < 0;
	}
}

float SGMarkerReference::CalculateThreshold(FName BoneName)
{
	float ratio = 0.25;
	TArray<float> HeightArray;
	for (int i = 0; i < AnimSequence->GetNumberOfFrames(); i++)
	{
		HeightArray.Add(FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimSequence, BoneName, i).GetLocation().Z);
	}
	HeightArray.Sort();
	return HeightArray[(int)(HeightArray.Num() * ratio)];
}


TArray<float> SGMarkerReference::GetCrossingTime(UAnimSequence * AnimationSequence, FName BoneName)
{
	TArray<FName> BonePath;
	FAnimCurveToolModule::FindBonePathToRoot(AnimationSequence, BoneName, BonePath);
	float MarkerTime = 0;
	float DeltaTime = AnimationSequence->GetPlayLength() / AnimationSequence->GetNumberOfFrames();
	FTransform Transform, OldTransform;
	bool IsOutsideContact = false;
	FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BonePath, OldTransform, AnimationSequence->GetNumberOfFrames()-1);
	//int debug;

	TArray<float> Result;
	FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BonePath, OldTransform, AnimationSequence->GetNumberOfFrames()-1);
	for (int i = 0; i < AnimationSequence->GetNumberOfFrames(); i++)
	{
		FAnimCurveToolModule::GetBoneTMRelativeToRoot(AnimationSequence, BonePath, Transform, i);
		if (IsCrossingPoint(Transform, OldTransform, Dir))
		{
			Result.Add(AnimationSequence->GetTimeAtFrame(i));
		}
		OldTransform = Transform;
	}

	if (Result.Num() == 0)
		UE_LOG(LogTemp, Warning, TEXT("No Crossing Point Found for %s %s"), *AnimationSequence->GetName(), *BoneName.ToString());
	return Result;
	
}

bool SGMarkerReference::IsCrossingPoint(FTransform Transform, FTransform OldTransform, Direction Direction)
{
const FVector v1 = Transform.GetLocation();
const FVector v2 = OldTransform.GetLocation();

// 根据方向判断当前帧是否为脚部骨骼越过根骨骼的时刻
if (Direction == Direction::f || Direction == Direction::rf || Direction == Direction::lf)
{
return v1.Y > 0 && v2.Y < 0;
}
else if (Direction == Direction::b || Direction == Direction::rb || Direction == Direction::lb)
{
return v1.Y < 0 && v2.Y > 0;
}
else if (Direction == Direction::r)
{
return v1.X < 0 && v2.X > 0;
}
else
{
return v1.X > 0 && v2.X < 0;
}
}
*/




#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FAnimCurveToolModule, AnimCurveTool)