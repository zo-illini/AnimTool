// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include <ios>

#include "Modules/ModuleManager.h"
#include "Animation/AnimSequence.h"
#include "AssetThumbnail.h"
#include "PropertyCustomizationHelpers.h"
#include "PropertyEditing.h"
#include "AssetRegistryModule.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AnimationModifier.h"
#include "Programs/UnrealLightmass/Private/ImportExport/3DVisualizer.h"
#include "Programs/UnrealLightmass/Private/ImportExport/3DVisualizer.h"
#include "UObject/UObjectGlobals.h"
#include "AnimationUtils.h"
#include "IContentBrowserSingleton.h"

class FToolBarBuilder;
class FMenuBuilder;

// 动画运动方向的标签枚举
enum Direction {l, r, f, b, lf, rf, lb, rb};

// 脚部的基准区间，由左右两个基准时间点组成，同时记录了该区间的左右脚先后顺序，以及是否跨越最后一帧
struct FootInterval
{
	float Left;
	float Right;
	bool IsOrderLeftRight;
	bool IsWrapped;

	FootInterval(float LeftSide, float RightSide, bool Order)
	{
		Left = LeftSide;
		Right = RightSide;
		IsOrderLeftRight = Order;
		IsWrapped = (LeftSide > RightSide);
	}
};

// 动画基准组，保存了一个动画序列及其双腿骨骼的所有基准区间
// 用于计算并输出AnimCurveToolModule需求的结果，一般为同步组标记与动画通知的时间
class SGMarkerReference
{
public:
	
	// 同步组的构造函数，在其中进行步态位置的预计算
	SGMarkerReference(UAnimSequence* AnimSequence, FName LeftFoot, FName RightFoot);

	// 根据动画命名判断动画的移动方向
	Direction GetAnimDirection();

	// 根据输入时间，找到其所在的区间，并计算在区间中的比例以及区间为左-右脚，还是右-左脚
	bool GetRatioFromTime(float Time, float & RefRatio, bool & IsOrderLeftRight);

	// 根据输入的比例，计算在每一个区间中该比例的对应时间并返回，左-右顺序用于筛选区间
	bool GetTimeFromRatio(float RefRatio, bool IsOrderLeftRight, TArray<float> & Time);

	// 计算腿部骨骼改变运动方向的而函数
	TArray<float> GetContactTimeFromTurning(UAnimSequence * AnimSequence, FName BoneName);

	// 根据动画方向标签，判断本帧是否为改变方向的点
	bool IsTurningPoint(FTransform LastFrame, FTransform CurFrame, FTransform NextFrame);
	
	// 根据z轴高度计算基准点的方案的相关函数，目前不再使用
	//TArray<float> GetContactTime(UAnimSequence * AnimSequence, FName BoneName, float Threshold);
	// 根据动画方向标签，判断目标帧对比上一帧在移动方向上的位移
	//float GetDisplacementAtFrame(int i, FName BoneName);
	// 根据动画方向标签，判断累积位移的方向是否正确
	//bool IsCumulativeDistanceValid(float Distance);
	// 计算高度阈值的函数
	//float CalculateThreshold(FName BoneName);

	// 计算腿部与根骨骼交叉的时间的函数，目前已不再使用
	//TArray<float> GetCrossingTime(UAnimSequence * AnimSequence, FName FNameBone);
	// 根据当前帧与上一帧的变换，判断当前帧是否为脚部骨骼越过根骨骼的时刻
	//bool IsCrossingPoint(FTransform Transform, FTransform OldTransform, Direction Dir);
	
	UAnimSequence * AnimSequence;
	float Tolerance;
	Direction Dir;
	bool bIsValid;
	// Sorted Array for Markers
	TArray<float> LeftMarkers;
	TArray<float> RightMarkers;
	TArray<FootInterval> Intervals;
	//float LeftThreshold, RightThreshold;
};

class FAnimCurveToolModule : public IModuleInterface, public TSharedFromThis<FAnimCurveToolModule>
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** This function will be bound to Command (by default it will bring up plugin window) */
	void PluginButtonClicked();

private:
	/* 与动画选择模块相关的变量与方法 */
	/* 从内容浏览器中添加动画 */
	FReply AddFromContentBrowser();
	
	/* 清除当前组内的动画 */
	FReply ResetSelectedAnimGroup();

	/* 构架用于动画选择的UI控件 */
	TSharedRef<SWidget> MakeAnimPicker();

	
	TArray<UAnimSequence*> SelectedAnimGroup;
	TSharedPtr<SWidget> AnimContentPicker;
	TSharedPtr<STextBlock> SelectedAnimGroupPreview;
	

/*  用于提取动画曲线的工具不再使用了   */
/*
protected:
	//Entry function for creating extractor widget
	TSharedRef<SWidget> MakeCurveExtractor();
	
	TSharedRef<SWidget> OnGetAnimMenu();
	void OnAnimAssetSelected(const FAssetData& AssetData);

	FReply ExtractBoneCurve();

	// Functions that work with the editable text created for curve extractor
	FText GetBoneName() const;
	void OnBoneNameCommitted(const FText& InText, ETextCommit::Type CommitInfo);
	FText GetTransScale() const;
	void OnTransScaleCommitted(const FText& InText, ETextCommit::Type CommitInfo);
	FText GetRotScale() const;
	void OnRotScaleCommitted(const FText& InText, ETextCommit::Type CommitInfo);

	// Functions that help with creating output selection checkboxes
	ECheckBoxState IsOutputBoxChecked(FString Type, FString Axis) const;
	void OnOutputCheckboxChanged(ECheckBoxState NewState, FString Type, FString Axis);
	TSharedRef<SWidget> MakeOutputCheckbox(FString Type, FString Axis);
	TSharedRef<SWidget> MakeOutputCheckboxRow(FString Type);
	void OnSavePathSelected(const FString& NewPath);

	// Functions that process and save the extracted curves
	bool FillCurveVector();
	void SaveCurve();
	void SaveCurveFloat(FString Type, FString Axis) const;

private:
	UAnimSequence * AnimSequenceToExtract = nullptr;
	TArray<FVector> TransCurve;
	TArray<FVector> RotCurve;
	TArray<FVector> ScaleCurve;
	FText TransScale;
	FText RotScale;
	FString SavePath;

	// cache for created widget
	TSharedPtr<SWidget> SelectAnimSequenceWidgetPtr;
	TSharedPtr<SComboButton> SelectAnimSequenceButtonPtr;
	TSharedPtr<FAssetThumbnail> AnimThumbnailPtr;
	TSharedPtr<FAssetThumbnailPool> AnimThumbnailPoolPtr;
	TSharedPtr<SWidget> AnimSequencePicker;

	FName TargetBoneName;

	TMap<FString, bool> OutputBoxStateMap;*/


protected:

	/* 构架用于动画播速缩放的UI控件 */
	TSharedRef<SWidget> MakePlaySpeedScaler();

	//void OnAnimToScalePathSelected(const FString& NewPath);
	//bool UpdateFilteredAnim(TArray<UAnimSequence*> &, TSharedRef<STextBlock>, FString);

	/* 读取动画选择模块中的SelectedAnimGroup，更新AnimGroupToScale */
	void UpdateAnimGroupToScale();
	
	/*  判断动画是否通过筛选的函数，目前的筛选规则为前后缀*/
	bool CheckShouldSelectAnim(FAssetData Asset) const;
	bool CheckShouldSelectAnim(UAnimSequence* AnimSequence) const;

	/* 根据输入动画序列，更新文字控件内容的方法 */
	void UpdatePreviewText(TArray<UAnimSequence *> &, TSharedRef<STextBlock>);

	/* 不同Editable Text控件的显示与修改时的回调 */
	FText GetAnimPrefix() const;
	void OnAnimPrefixCommitted(const FText& InText, ETextCommit::Type CommitInfo);
	FText GetAnimPostfix() const;
	void OnAnimPostfixCommitted(const FText& InText, ETextCommit::Type CommitInfo);
	FText GetRateScale() const;
	void OnRateScaleCommitted(const FText& InText, ETextCommit::Type CommitInfo);
	FText GetRootMotionSpeed() const;
	void OnRootMotionCommitted(const FText& InText, ETextCommit::Type CommitInfo);

	/* 按当前RateScale修改动画播放速率，为按钮的回调 */
	FReply ApplyRateScale() const;
	/* 按目标RootMotionSpeed修改动画播放速率，为按钮的回调*/
	FReply ApplyRootMotionSpeed() const;
	

private:
	FString AnimToScalePath;
	TArray<UAnimSequence *> AnimSequencesToScale; 
	TSharedPtr<STextBlock> AnimSequencesToScalePreview;
	FText AnimPrefix;
	FText AnimPostfix;
	FText RateScale;
	FText RootMotionSpeed;


protected:
	/*
	void OnAnimToMarkPathSelected(const FString& NewPath);
	void GetSelectedAnimSequence(TArray<UAnimSequence*>& SelectedAnim);
	*/

	FText GetTolerance() const;
	void OnToleranceCommitted(const FText& InText, ETextCommit::Type CommitInfo);

	/* 左右骨骼名与参考轨道相关，Editable Text的显示与更改回调*/
	FText GetLeftFootBone() const { return FText::FromName(FootLeft);}
	void OnLeftFootBoneCommitted(const FText& InText, ETextCommit::Type CommitInfo);
	FText GetRightFootBone() const { return FText::FromName(FootRight);}
	void OnRightFootBoneCommitted(const FText& InText, ETextCommit::Type CommitInfo);
	FText GetRefTrackName() const;
	void OnRefTrackNameCommitted(const FText& InText, ETextCommit::Type CommitInfo);

	/* 构建用于步态同步的UI控件 */
	TSharedRef<SWidget> MakeSGMarkerWidget();

	/* 将动画序列进行预计算并加入同步组TMap中保存 */
	FReply AddAllReferenceGroup();
	void AddToReferenceGroup(TArray<UAnimSequence*> &, TMap<UAnimSequence*, SGMarkerReference>&);

	/* 清空当前同步组的方法，为按钮的回调 */
	FReply ClearReferenceGroup();

	/* 选择参考动画时，弹出菜单的回调 */
	TSharedRef<SWidget> OnGetRefAnimMenu();

	/* 选择参考动画时，选择后的回调 */
	void OnRefAnimAssetSelected(const FAssetData& AssetData);

	/* 根据同步组信息与参考动画，复制轨道，动画通知与同步标记*/
	FReply SyncReferenceGroupOnClicked();
	void SyncReferenceGroup(UAnimSequence * RefAnimSequence, FName TrackName);

	/* 添加默认的同步组标签，时间值由底层算法决定，目前为双腿分别经过root的时刻 */
	FReply AddDefaultMarkerForReferenceGroup();

	/* 用于添加同步组标记的helper function */
	void AddContactMarker(UAnimSequence * AnimSequence, FName TrackName, FName MarkerName, float MarkerTime);
	void AddAnimationSyncMarker(UAnimSequence* AnimationSequence, FName MarkerName, float Time, FName TrackName);

	/* 用于添加动画通知的helper function */
	UAnimNotify* AddAnimationNotifyEvent(UAnimSequence* AnimationSequence, FName NotifyTrackName, float StartTime, TSubclassOf<UAnimNotify> NotifyClass);

	/* 用于处理动画轨道的helper function */
	void AddAnimationNotifyTrack(UAnimSequence* AnimationSequence, FName NotifyTrackName, FLinearColor TrackColor);
	void RemoveAnimationNotifyTrack(UAnimSequence* AnimationSequence, FName NotifyTrackName);
	int32 GetTrackIndexForAnimationNotifyTrackName(const UAnimSequence* AnimationSequence, FName NotifyTrackName);

public:

	/* 用于计算骨骼相对于根的相对变换 */
	static FTransform GetBoneTMRelativeToRoot(UAnimSequence* AnimationSequence, FName BoneName, int Frame);

	/* 计算并返回选定骨骼到根骨骼的路径 */
	static void FindBonePathToRoot(const UAnimSequence* AnimationSequence, FName BoneName, TArray<FName>& BonePath);

	/* 获取轨道索引的helper function */
	static int32 GetAnimTrackIndexForSkeletonBone(const int32 InSkeletonBoneIndex, const TArray<FTrackToSkeletonMap>& TrackToSkelMap);



private:
	FName FootLeft, FootRight;
	//float ContactTolerance;
	FText ContactTolerance;
	FString AnimToMarkPath;
	UAnimSequence * RefAnimSeuquence;
	TArray<UAnimSequence *> AutoMarkAnimGroup;
	//TArray<SGMarkerReference> AnimReferenceGroup;
	TMap<UAnimSequence*, SGMarkerReference> AnimReferenceGroup;
	TSharedPtr<SComboButton> SelectRefAnimButtonPtr;
	TSharedPtr<SWidget> SelectRefAnimWidgetPtr;
	TSharedPtr<FAssetThumbnail> RefAnimThumbnailPtr;
	TSharedPtr<FAssetThumbnailPool> RefAnimThumbnailPoolPtr;
	TSharedPtr<STextBlock> AnimSequencesToMarkPreview;
	TSharedPtr<STextBlock> AnimReferenceGroupPreview;
	FName RefTrackName;


private:
	void InitializeMembers();
	void RegisterMenus();
	TSharedRef<class SDockTab> OnSpawnPluginTab(const class FSpawnTabArgs& SpawnTabArgs);

private:
	TSharedPtr<class FUICommandList> PluginCommands;
};
