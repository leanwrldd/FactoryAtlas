#include "AtlasMapHud.h"

#include "AtlasSubsystem.h"
#include "FactoryAtlas.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

using namespace Atlas;

namespace
{
	// Taken from the game's own tooltip: orange title band over a near-black body.
	const FLinearColor Accent(FColor(226, 142, 66));
	const FLinearColor BodyColor(FColor(16, 16, 17, 240));
	const FLinearColor RowColor(FColor(38, 38, 40));
	const FLinearColor TextMain(FColor(240, 240, 240));
	const FLinearColor TextDim(FColor(168, 168, 170));
	constexpr float MinHeightMetres = -200.f;
	constexpr float MaxHeightMetres = 3895.f;

	/** Built only from solid brushes, because Slate's stock images are not in the cooked game. */
	struct FAtlasStyle
	{
		TSharedPtr<FStandaloneCompositeFont> Composite;
		FSlateFontInfo Font;
		FSlateFontInfo FontBold;
		FSlateFontInfo FontSmall;

		FSlateColorBrush White{ FLinearColor::White };
		FSlateColorBrush Clear{ FLinearColor::Transparent };
		FSlateColorBrush Body{ BodyColor };
		FSlateColorBrush Header{ Accent };
		FSlateColorBrush RowHover{ FLinearColor(1.f, 1.f, 1.f, 0.06f) };
		FSlateColorBrush SliderBar{ RowColor };
		FSlateRoundedBoxBrush ToggleOn{ Accent, 2.f };
		FSlateRoundedBoxBrush ToggleOff{ RowColor, 2.f };
		FSlateRoundedBoxBrush SliderThumb{ Accent, 2.f };
		FSlateRoundedBoxBrush Dot{ FLinearColor::White, 4.f };

		FButtonStyle Flat;
		FButtonStyle Tinted;
		FSliderStyle Slider;
		FScrollBoxStyle Scroll;
		FScrollBarStyle ScrollBar;

		FAtlasStyle()
		{
			const UObject* Face = LoadObject<UObject>(nullptr, TEXT("/Game/FactoryGame/Interface/Font/Heebo-Regular.Heebo-Regular"));
			const UObject* FaceBold = LoadObject<UObject>(nullptr, TEXT("/Game/FactoryGame/Interface/Font/Heebo-Bold.Heebo-Bold"));
			if (Face && FaceBold)
			{
				Composite = MakeShared<FStandaloneCompositeFont>();
				FTypefaceEntry& Regular = Composite->DefaultTypeface.Fonts.Add_GetRef(FTypefaceEntry(TEXT("Regular")));
				Regular.Font = FFontData(Face);
				FTypefaceEntry& Bold = Composite->DefaultTypeface.Fonts.Add_GetRef(FTypefaceEntry(TEXT("Bold")));
				Bold.Font = FFontData(FaceBold);
				Font = FSlateFontInfo(Composite, 10.f, TEXT("Regular"));
				FontBold = FSlateFontInfo(Composite, 11.f, TEXT("Bold"));
				FontSmall = FSlateFontInfo(Composite, 9.f, TEXT("Regular"));
			}
			else
			{
				Font = FCoreStyle::GetDefaultFontStyle("Regular", 10);
				FontBold = FCoreStyle::GetDefaultFontStyle("Bold", 11);
				FontSmall = FCoreStyle::GetDefaultFontStyle("Regular", 9);
			}

			Flat.SetNormal(Clear).SetHovered(RowHover).SetPressed(RowHover).SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));
			Tinted.SetNormal(White).SetHovered(White).SetPressed(White).SetNormalPadding(FMargin(0.f)).SetPressedPadding(FMargin(0.f));

			Slider.SetNormalBarImage(SliderBar).SetHoveredBarImage(SliderBar).SetDisabledBarImage(SliderBar)
				.SetNormalThumbImage(SliderThumb).SetHoveredThumbImage(SliderThumb).SetDisabledThumbImage(SliderThumb).SetBarThickness(4.f);
			Slider.NormalThumbImage.ImageSize = FVector2f(8.f, 14.f);
			Slider.HoveredThumbImage.ImageSize = FVector2f(8.f, 14.f);
			Slider.DisabledThumbImage.ImageSize = FVector2f(8.f, 14.f);

			Scroll.SetTopShadowBrush(FSlateNoResource()).SetBottomShadowBrush(FSlateNoResource())
				.SetLeftShadowBrush(FSlateNoResource()).SetRightShadowBrush(FSlateNoResource());
		}

		static const FAtlasStyle& Get()
		{
			// Lives for the whole run: the font asset must outlast every widget.
			static FAtlasStyle* Style = new FAtlasStyle();
			return *Style;
		}
	};

	UAtlasSubsystem* Sub()
	{
		return UAtlasSubsystem::GetActive();
	}

	/**
	 * Slate hands keyboard focus to a widget under the cursor on every click unless the reply names one.
	 * The game's map tab reacts to any focus change (it toggles its focus highlight), which made the screen
	 * flash. Naming the widget that already has focus is a no-op, so clicks here leave focus alone.
	 */
	FReply KeepFocus(FReply Reply, const FPointerEvent& Event)
	{
		if (Reply.IsEventHandled() && !Reply.GetUserFocusRecepient().IsValid())
		{
			if (const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetUserFocusedWidget(Event.GetUserIndex()))
			{
				Reply.SetUserFocus(Focused.ToSharedRef(), EFocusCause::Mouse);
			}
		}
		return Reply;
	}

	class SAtlasButton : public SButton
	{
	public:
		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			return KeepFocus(SButton::OnMouseButtonDown(Geometry, Event), Event);
		}
		virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			return KeepFocus(SButton::OnMouseButtonDoubleClick(Geometry, Event), Event);
		}
	};

	class SAtlasSlider : public SSlider
	{
	public:
		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			return KeepFocus(SSlider::OnMouseButtonDown(Geometry, Event), Event);
		}
	};

	const FColor ColorChoices[] = {
		FColor(226, 142, 66), FColor(255, 200, 40), FColor(170, 230, 60), FColor(60, 200, 90),
		FColor(40, 200, 180), FColor(60, 190, 255), FColor(50, 110, 240), FColor(140, 90, 240),
		FColor(220, 80, 220), FColor(255, 120, 170), FColor(235, 60, 60), FColor(150, 95, 55),
		FColor(245, 245, 245), FColor(170, 170, 175), FColor(100, 102, 110), FColor(45, 47, 52),
	};

	TSharedRef<STextBlock> MakeLabel(const FText& Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		return SNew(STextBlock).Text(Text).Font(Font).ColorAndOpacity(FSlateColor(Color));
	}

	TSharedRef<STextBlock> MakeLabel(const TCHAR* Text, const FSlateFontInfo& Font, const FLinearColor& Color)
	{
		return MakeLabel(FText::FromString(Text), Font, Color);
	}

	TSharedRef<SWidget> MakeToggle(TFunction<bool()> IsOn, TFunction<void()> OnClick)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SAtlasButton)
			.ButtonStyle(&Style.Flat)
			.IsFocusable(false)
			.ContentPadding(FMargin(0.f))
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
				[
					SNew(SImage).Image_Lambda([IsOn, &Style]() -> const FSlateBrush*
					{
						return IsOn() ? &Style.ToggleOn : &Style.ToggleOff;
					})
				]
			];
	}

	TSharedRef<SWidget> MakeTextButton(const FText& Text, TFunction<bool()> IsActive, TFunction<void()> OnClick)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SAtlasButton)
			.ButtonStyle(&Style.Tinted)
			.IsFocusable(false)
			.ButtonColorAndOpacity_Lambda([IsActive]() { return FSlateColor(IsActive() ? Accent : RowColor); })
			.HAlign(HAlign_Center)
			.ContentPadding(FMargin(4.f, 3.f))
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				MakeLabel(Text, Style.Font, TextMain)
			];
	}

	/** The panel's background swallows every click, so nothing reaches the map underneath it. */
	class SAtlasPanelBorder : public SBorder
	{
	public:
		virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Event) override { return KeepFocus(FReply::Handled(), Event); }
		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
		virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent& Event) override { return KeepFocus(FReply::Handled(), Event); }
		virtual FReply OnMouseWheel(const FGeometry&, const FPointerEvent&) override { return FReply::Handled(); }
	};

	/** One track with two handles. Values run from 0 to 1. */
	class SAtlasRangeSlider : public SLeafWidget
	{
	public:
		DECLARE_DELEGATE_TwoParams(FOnRangeChanged, float, float);

		SLATE_BEGIN_ARGS(SAtlasRangeSlider) {}
			SLATE_EVENT(FOnRangeChanged, OnChanged)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			OnChanged = InArgs._OnChanged;
		}

		float GetLow() const { return Low; }
		float GetHigh() const { return High; }

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(100.0, 20.0);
		}

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
			int32 LayerId, const FWidgetStyle&, bool) const override
		{
			const FAtlasStyle& Style = FAtlasStyle::Get();
			const FVector2f Size(Geometry.GetLocalSize());
			const float Track = TrackWidth(Size.X);
			const float LowX = ThumbWidth * 0.5f + Low * Track;
			const float HighX = ThumbWidth * 0.5f + High * Track;
			const float BarY = (Size.Y - BarHeight) * 0.5f;

			const auto Box = [&](int32 Layer, float X, float Y, float Width, float Height, const FLinearColor& Color)
			{
				FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(FVector2f(Width, Height), FSlateLayoutTransform(FVector2f(X, Y))),
					&Style.White, ESlateDrawEffect::None, Color);
			};

			Box(LayerId, 0.f, BarY, Size.X, BarHeight, RowColor);
			Box(LayerId + 1, LowX, BarY, HighX - LowX, BarHeight, Accent);
			Box(LayerId + 2, LowX - ThumbWidth * 0.5f, 0.f, ThumbWidth, Size.Y, Accent);
			Box(LayerId + 2, HighX - ThumbWidth * 0.5f, 0.f, ThumbWidth, Size.Y, Accent);
			return LayerId + 2;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}

			const float Value = ValueAt(Geometry, Event);
			const float ToLow = FMath::Abs(Value - Low);
			const float ToHigh = FMath::Abs(Value - High);
			// When the handles touch, the side of the click decides which one moves.
			bDraggingLow = ToLow == ToHigh ? Value < Low : ToLow < ToHigh;
			Drag(Value);
			return KeepFocus(FReply::Handled().CaptureMouse(SharedThis(this)), Event);
		}

		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
		{
			return HasMouseCapture() ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
		}

		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (!HasMouseCapture())
			{
				return FReply::Unhandled();
			}
			Drag(ValueAt(Geometry, Event));
			return FReply::Handled();
		}

	private:
		static constexpr float ThumbWidth = 8.f;
		static constexpr float BarHeight = 4.f;

		static float TrackWidth(float Width)
		{
			return FMath::Max(Width - ThumbWidth, 1.f);
		}

		float ValueAt(const FGeometry& Geometry, const FPointerEvent& Event) const
		{
			const FVector2f Local(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
			return FMath::Clamp((Local.X - ThumbWidth * 0.5f) / TrackWidth(float(Geometry.GetLocalSize().X)), 0.f, 1.f);
		}

		void Drag(float Value)
		{
			if (bDraggingLow)
			{
				Low = FMath::Min(Value, High);
			}
			else
			{
				High = FMath::Max(Value, Low);
			}
			Invalidate(EInvalidateWidgetReason::Paint);
			OnChanged.ExecuteIfBound(Low, High);
		}

		FOnRangeChanged OnChanged;
		float Low = 0.f;
		float High = 1.f;
		bool bDraggingLow = true;
	};
}

class SAtlasHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAtlasHud) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		SetVisibility(EVisibility::SelfHitTestInvisible);

		ChildSlot
		[
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(FMargin(0.f, 6.f, 12.f, 0.f))
			[
				SNew(SVerticalBox)
				.Visibility(EVisibility::SelfHitTestInvisible)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SAssignNew(Panel, SAtlasPanelBorder)
					.BorderImage(&Style.Body)
					.Padding(FMargin(0.f))
					[
						SNew(SBox)
						.WidthOverride(290.f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[MakeHeader()]
							+ SVerticalBox::Slot().AutoHeight()[MakeBody()]
						]
					]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					MakeSelectionPanel()
				]
			]
		];
	}

	/** A left click on the map that did not drag: select the building under the cursor, or clear the selection. */
	void OnMapClick(const FVector2f& ScreenPosition)
	{
		UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || Atlas->IsPointerOverPanel() || !Atlas->IsPointOnMap(ScreenPosition))
		{
			return;
		}
		if (!Atlas->SelectHovered())
		{
			Atlas->ClearSelection();
		}
		ClockDraft.Reset();
		bRecipePicker = false;
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		UAtlasSubsystem* Atlas = Sub();
		if (!Atlas)
		{
			return;
		}

		// Measured from the panels' shapes, because a pressed button changes which widgets count as hovered.
		const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
		const bool bOverPanel = (Panel.IsValid() && Panel->GetTickSpaceGeometry().IsUnderLocation(Cursor))
			|| (SelectionPanel.IsValid() && Atlas->HasSelection() && SelectionPanel->GetTickSpaceGeometry().IsUnderLocation(Cursor));
		Atlas->SetPointerOverPanel(bOverPanel);

		// Read once a frame; every line of the configure panel shows a piece of it.
		State = Atlas->GetSelectionState();

		if (Atlas->ConsumeMenuDirty() || bListDirty)
		{
			bListDirty = false;
			RebuildList(*Atlas);
		}
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		const int32 Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

		const UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || !Atlas->GetSettings().bHoverLabel || !Atlas->GetHover().bValid || Atlas->GetHoverName().IsEmpty())
		{
			return Layer;
		}

		const FAtlasStyle& Style = FAtlasStyle::Get();
		const FHover& Hover = Atlas->GetHover();
		const FSettings& Settings = Atlas->GetSettings();

		const FString Title = Atlas->GetHoverName().ToString();
		TArray<FString> Lines;
		if (Settings.bHoverHeight)
		{
			Lines.Add(FString::Printf(TEXT("Height  %d m"), FMath::RoundToInt32(Hover.HeightMetres)));
		}
		if (Settings.bHoverCoordinates)
		{
			Lines.Add(FString::Printf(TEXT("X  %d    Y  %d"), FMath::RoundToInt32(Hover.WorldMetres.X), FMath::RoundToInt32(Hover.WorldMetres.Y)));
		}

		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2f Padding(9.f, 4.f);
		const FVector2f TitleSize(Measure->Measure(Title, Style.FontBold));
		const float TitleBand = TitleSize.Y + Padding.Y * 2.f;

		float Width = TitleSize.X;
		float LineHeight = 0.f;
		for (const FString& Line : Lines)
		{
			const FVector2f Size(Measure->Measure(Line, Style.Font));
			Width = FMath::Max(Width, Size.X);
			LineHeight = Size.Y;
		}
		const float BodyHeight = Lines.IsEmpty() ? 0.f : LineHeight * Lines.Num() + Padding.Y * 2.f;
		const FVector2f BoxSize(Width + Padding.X * 2.f, TitleBand + BodyHeight);

		const FVector2f LocalSize(AllottedGeometry.GetLocalSize());
		FVector2f Position = FVector2f(AllottedGeometry.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())) + FVector2f(16.f, 16.f);
		Position.X = FMath::Min(Position.X, LocalSize.X - BoxSize.X - 4.f);
		Position.Y = FMath::Min(Position.Y, LocalSize.Y - BoxSize.Y - 4.f);

		// MakeBox uses the tint passed here and ignores the brush's own colour.
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, BodyColor);
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 2, AllottedGeometry.ToPaintGeometry(FVector2f(BoxSize.X, TitleBand), FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, Accent);
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToPaintGeometry(TitleSize, FSlateLayoutTransform(Position + Padding)),
			Title, Style.FontBold, ESlateDrawEffect::None, FLinearColor::White);

		FVector2f LinePosition = Position + FVector2f(Padding.X, TitleBand + Padding.Y);
		for (const FString& Line : Lines)
		{
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToPaintGeometry(FVector2f(Width, LineHeight), FSlateLayoutTransform(LinePosition)),
				Line, Style.Font, ESlateDrawEffect::None, TextMain);
			LinePosition.Y += LineHeight;
		}
		return Layer + 3;
	}

private:
	TSharedRef<SWidget> MakeHeader()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SBorder)
			.BorderImage(&Style.Header)
			.Padding(FMargin(10.f, 5.f, 6.f, 5.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					MakeLabel(TEXT("Atlas"), Style.FontBold, FLinearColor::White)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SAtlasButton)
					.ButtonStyle(&Style.Flat)
					.IsFocusable(false)
					.ContentPadding(FMargin(6.f, 0.f))
					.OnClicked_Lambda([]() { if (Sub()) { Sub()->SetPanelOpen(!Sub()->IsPanelOpen()); } return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Style.Font).ColorAndOpacity(FSlateColor(FLinearColor::White))
						.Text_Lambda([]() { return FText::FromString(Sub() && Sub()->IsPanelOpen() ? TEXT("Hide") : TEXT("Show")); })
					]
				]
			];
	}

	TSharedRef<SWidget> MakeBody()
	{
		return SNew(SVerticalBox)
			.Visibility_Lambda([]() { return Sub() && Sub()->IsPanelOpen() ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)[MakeTab(TEXT("Layers"), false)]
				+ SHorizontalBox::Slot().FillWidth(1.f)[MakeTab(TEXT("Settings"), true)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 8.f, 10.f, 10.f))
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return bSettingsTab ? EVisibility::Collapsed : EVisibility::Visible; })
					[
						MakeLayersTab()
					]
				]
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { return bSettingsTab ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						MakeSettingsTab()
					]
				]
			];
	}

	TSharedRef<SWidget> MakeTab(const TCHAR* Label, bool bSettings)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SAtlasButton)
			.ButtonStyle(&Style.Flat)
			.IsFocusable(false)
			.ContentPadding(FMargin(0.f))
			.OnClicked_Lambda([this, bSettings]() { bSettingsTab = bSettings; return FReply::Handled(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 5.f))
				[
					SNew(STextBlock).Text(FText::FromString(Label)).Font(Style.Font)
					.ColorAndOpacity_Lambda([this, bSettings]() { return FSlateColor(bSettingsTab == bSettings ? TextMain : TextDim); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(SBox).HeightOverride(2.f)
					[
						SNew(SImage).Image(&Style.White)
						.ColorAndOpacity_Lambda([this, bSettings]() { return FSlateColor(bSettingsTab == bSettings ? Accent : RowColor); })
					]
				]
			];
	}

	TSharedRef<SWidget> MakeLayersTab()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f, 8.f, 0.f))
				[
					MakeToggle(
						[]() { return Sub() && Sub()->IsLayerEnabled(); },
						[]() { if (Sub()) { Sub()->SetLayerEnabled(!Sub()->IsLayerEnabled()); } })
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					MakeLabel(TEXT("Show buildings"), Style.Font, TextMain)
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				MakeSettingRow(TEXT("Show radioactivity"), &FSettings::bRadiation)
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 2.f))
			[
				MakeLabel(TEXT("Collectibles left to find"), Style.FontSmall, TextDim)
			]
			+ SVerticalBox::Slot().AutoHeight()[MakeCollectibleRow(ECollectible::Somersloop, MarkerSomersloop)]
			+ SVerticalBox::Slot().AutoHeight()[MakeCollectibleRow(ECollectible::MercerSphere, MarkerMercerSphere)]
			+ SVerticalBox::Slot().AutoHeight()[MakeCollectibleRow(ECollectible::HardDrive, MarkerHardDrive)]
			+ SVerticalBox::Slot().AutoHeight()[MakeCollectibleRow(ECollectible::PowerSlug, MarkerSlugBlue)]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 2.f))
			[
				MakeLabel(TEXT("World"), Style.FontSmall, TextDim)
			]
			+ SVerticalBox::Slot().AutoHeight()[MakeFeatureRow(EFeature::ResourceNodes)]
			+ SVerticalBox::Slot().AutoHeight()[MakeFeatureRow(EFeature::ResourceWells)]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(ResourceList, SVerticalBox)
				.Visibility_Lambda([]()
				{
					return Sub() && (Sub()->IsFeatureShown(EFeature::ResourceNodes) || Sub()->IsFeatureShown(EFeature::ResourceWells))
						? EVisibility::Visible : EVisibility::Collapsed;
				})
			]
			+ SVerticalBox::Slot().AutoHeight()[MakeFeatureRow(EFeature::SporeFlowers)]
			+ SVerticalBox::Slot().AutoHeight()[MakeFeatureRow(EFeature::GasPillars)]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 3.f, 0.f))[MakeModeButton(TEXT("Category colors"), false)]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 0.f, 0.f))[MakeModeButton(TEXT("Paint colors"), true)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 3.f))
			[
				SNew(STextBlock).Font(Style.Font).ColorAndOpacity(FSlateColor(TextDim))
				.Text_Lambda([this]() { return HeightText(); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(HeightSlider, SAtlasRangeSlider)
				.OnChanged_Lambda([this](float, float) { ApplyHeight(); })
			]
			+ SVerticalBox::Slot().AutoHeight().MaxHeight(420.f).Padding(FMargin(0.f, 8.f, 0.f, 0.f))
			[
				SNew(SScrollBox)
				.Style(&Style.Scroll)
				.ScrollBarStyle(&Style.ScrollBar)
				.ScrollBarVisibility(EVisibility::Collapsed)
				+ SScrollBox::Slot()
				[
					SAssignNew(List, SVerticalBox)
				]
			];
	}

	TSharedRef<SWidget> MakeSettingRow(const TCHAR* Label, bool FSettings::* Member)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 3.f, 8.f, 3.f))
			[
				MakeToggle(
					[Member]() { return Sub() && Sub()->GetSettings().*Member; },
					[Member]()
					{
						if (UAtlasSubsystem* Atlas = Sub())
						{
							Atlas->EditSettings().*Member = !(Atlas->GetSettings().*Member);
							Atlas->SaveSettings();
						}
					})
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				MakeLabel(Label, Style.Font, TextMain)
			];
	}

	TSharedRef<SWidget> MakeSelectionPanel()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const auto Line = [this, &Style](TFunction<FString()> Text)
		{
			return SNew(STextBlock).Font(Style.Font).ColorAndOpacity(FSlateColor(TextMain))
				.Text_Lambda([Text]() { return FText::FromString(Text()); });
		};
		const auto Step = [this](const TCHAR* Label, float Points)
		{
			return MakeTextButton(FText::FromString(Label), []() { return false; },
				[this, Points]() { if (Sub()) { Sub()->SetSelectedClock(State.Pending + Points / 100.f); } });
		};
		const auto CanEdit = [this]() { return State.bValid && State.bAuthority && State.bCanChangeClock && State.Max > State.Min; };

		return SAssignNew(SelectionPanel, SAtlasPanelBorder)
			.BorderImage(&Style.Body)
			.Padding(FMargin(0.f))
			.Visibility_Lambda([this]() { return State.bValid ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SNew(SBox)
				.WidthOverride(290.f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(&Style.Header)
						.Padding(FMargin(10.f, 5.f, 6.f, 5.f))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock).Font(Style.FontBold).ColorAndOpacity(FSlateColor(FLinearColor::White))
								.Text_Lambda([this]() { return State.Name; })
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								SNew(SAtlasButton)
								.ButtonStyle(&Style.Flat)
								.IsFocusable(false)
								.ContentPadding(FMargin(6.f, 0.f))
								.OnClicked_Lambda([this]() { if (Sub()) { Sub()->ClearSelection(); } ClockDraft.Reset(); return FReply::Handled(); })
								[
									MakeLabel(TEXT("Close"), Style.Font, FLinearColor::White)
								]
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 8.f, 10.f, 10.f))
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeRecipeSection()
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							Line([this]()
							{
								const float Shown = ClockDraft.IsSet() ? ClockDraft.GetValue() : State.Pending;
								FString Text = FString::Printf(TEXT("Clock speed  %.1f%%"), Shown * 100.f);
								if (!ClockDraft.IsSet() && !FMath::IsNearlyEqual(State.Pending, State.Current, 0.0005f))
								{
									Text += FString::Printf(TEXT("  (now %.1f%%)"), State.Current * 100.f);
								}
								return Text;
							})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 4.f))
						[
							SNew(SAtlasSlider)
							.Style(&Style.Slider)
							.IsFocusable(false)
							.IsEnabled_Lambda(CanEdit)
							.Value_Lambda([this]()
							{
								const float Value = ClockDraft.IsSet() ? ClockDraft.GetValue() : State.Pending;
								return State.Max > State.Min ? (Value - State.Min) / (State.Max - State.Min) : 1.f;
							})
							.OnValueChanged_Lambda([this](float Value)
							{
								// Whole tenths of a percent, like the numbers players type into the game's own field.
								ClockDraft = FMath::RoundToFloat(FMath::Lerp(State.Min, State.Max, Value) * 1000.f) / 1000.f;
							})
							.OnMouseCaptureEnd_Lambda([this]()
							{
								if (ClockDraft.IsSet() && Sub())
								{
									Sub()->SetSelectedClock(ClockDraft.GetValue());
								}
								ClockDraft.Reset();
							})
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							SNew(SHorizontalBox)
							.IsEnabled_Lambda(CanEdit)
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 3.f, 0.f))[Step(TEXT("-10"), -10.f)]
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 3.f, 0.f))[Step(TEXT("-1"), -1.f)]
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 3.f, 0.f))[Step(TEXT("+1"), 1.f)]
							+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 0.f, 0.f))[Step(TEXT("+10"), 10.f)]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
						[
							Line([this]()
							{
								return FString::Printf(TEXT("Power shards  %d    (range %.0f%% to %.0f%%)"), State.Shards, State.Min * 100.f, State.Max * 100.f);
							})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 3.f, 0.f, 0.f))
						[
							Line([this]()
							{
								if (!State.bCanBoost)
								{
									return FString(TEXT("Somersloop  not supported"));
								}
								return State.MaxBoost > 1.001f
									? FString::Printf(TEXT("Somersloop  installed, output x%.1f"), State.MaxBoost)
									: FString(TEXT("Somersloop  none"));
							})
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 3.f, 0.f, 0.f))
						[
							Line([this]() { return FString::Printf(TEXT("Productivity  %d%%"), FMath::RoundToInt32(State.Productivity * 100.f)); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
						[
							SNew(SHorizontalBox)
							.IsEnabled_Lambda([this]() { return State.bValid && State.bAuthority; })
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 0.f, 8.f, 0.f))
							[
								MakeToggle([this]() { return State.bPaused; },
									[this]() { if (Sub()) { Sub()->SetSelectedPaused(!State.bPaused); } })
							]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								MakeLabel(TEXT("Production paused"), Style.Font, TextMain)
							]
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
						[
							SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)
							.Text_Lambda([this]()
							{
								if (State.bValid && !State.bAuthority)
								{
									return FText::FromString(TEXT("Only the host can change buildings."));
								}
								if (State.bValid && !State.bCanChangeClock)
								{
									return FText::FromString(TEXT("This building has no clock speed."));
								}
								return FText::FromString(TEXT("Clock changes apply after the current production cycle. Add or remove power shards at the building itself."));
							})
						]
					]
				]
			];
	}

	TSharedRef<SWidget> MakeRecipeSection()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SVerticalBox)
			.Visibility_Lambda([this]() { return State.bValid && State.bHasRecipes ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(Style.FontBold).ColorAndOpacity(FSlateColor(TextMain))
					.Text_Lambda([this]() { return State.Recipe ? State.RecipeName : FText::FromString(TEXT("No recipe")); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(70.f)
					.IsEnabled_Lambda([this]() { return State.bAuthority; })
					[
						MakeTextButton(FText::FromString(TEXT("Change")), [this]() { return bRecipePicker; },
							[this]()
							{
								bRecipePicker = !bRecipePicker;
								if (bRecipePicker)
								{
									FillRecipeList();
								}
							})
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f, 0.f, 0.f))
			[
				SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)
				.Text_Lambda([this]() { return FText::FromString(State.RecipeSummary); })
				.Visibility_Lambda([this]() { return State.RecipeSummary.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			]
			+ SVerticalBox::Slot().AutoHeight().MaxHeight(240.f).Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(&Style.White)
				.BorderBackgroundColor(FSlateColor(RowColor))
				.Padding(FMargin(2.f))
				.Visibility_Lambda([this]() { return bRecipePicker ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SScrollBox)
					.Style(&Style.Scroll)
					.ScrollBarStyle(&Style.ScrollBar)
					.ScrollBarVisibility(EVisibility::Collapsed)
					+ SScrollBox::Slot()
					[
						SAssignNew(RecipeList, SVerticalBox)
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)
				.Text(FText::FromString(TEXT("Items inside the machine go to your inventory, as when you change it at the machine.")))
				.Visibility_Lambda([this]() { return bRecipePicker ? EVisibility::Visible : EVisibility::Collapsed; })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 8.f))
			[
				SNew(SBox).HeightOverride(1.f)
				[
					SNew(SImage).Image(&Style.White).ColorAndOpacity(FSlateColor(RowColor))
				]
			];
	}

	void FillRecipeList()
	{
		if (!RecipeList || !Sub())
		{
			return;
		}
		const FAtlasStyle& Style = FAtlasStyle::Get();
		RecipeList->ClearChildren();

		const TArray<FRecipeOption> Options = Sub()->GetSelectedRecipeOptions();
		if (Options.IsEmpty())
		{
			RecipeList->AddSlot().AutoHeight().Padding(FMargin(6.f, 4.f))[MakeLabel(TEXT("No recipes available"), Style.FontSmall, TextDim)];
		}
		for (const FRecipeOption& Option : Options)
		{
			UClass* Recipe = Option.Recipe;
			const bool bCurrent = Recipe == State.Recipe;
			RecipeList->AddSlot().AutoHeight()
			[
				SNew(SAtlasButton)
				.ButtonStyle(&Style.Flat)
				.IsFocusable(false)
				.ContentPadding(FMargin(6.f, 4.f))
				.OnClicked_Lambda([this, Recipe]()
				{
					if (Sub())
					{
						Sub()->SetSelectedRecipe(Recipe);
					}
					bRecipePicker = false;
					return FReply::Handled();
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						MakeLabel(Option.Name, Style.Font, bCurrent ? Accent : TextMain)
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock).Text(FText::FromString(Option.Summary)).Font(Style.FontSmall)
						.ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)
					]
				]
			];
		}
	}

	TSharedRef<SWidget> MakeFeatureRow(EFeature Feature)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const bool bSeenOnly = Feature == EFeature::SporeFlowers || Feature == EFeature::GasPillars;
		return SNew(SHorizontalBox)
			.ToolTipText(bSeenOnly
				? FText::FromString(TEXT("Only exist in the game while their part of the world is loaded. Atlas remembers each one it has seen, in every save."))
				: FText::GetEmpty())
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 3.f, 8.f, 3.f))
			[
				MakeToggle(
					[Feature]() { return Sub() && Sub()->IsFeatureShown(Feature); },
					[Feature]() { if (Sub()) { Sub()->SetFeatureShown(Feature, !Sub()->IsFeatureShown(Feature)); } })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				MakeLabel(FeatureName(Feature), Style.Font, TextMain)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim))
				.Text_Lambda([Feature, bSeenOnly]()
				{
					if (!Sub() || !Sub()->IsFeatureShown(Feature))
					{
						return FText::GetEmpty();
					}
					const FText Count = FText::AsNumber(Sub()->GetFeatureCount(Feature));
					return bSeenOnly ? FText::Format(FText::FromString(TEXT("{0} seen")), Count) : Count;
				})
			];
	}

	void RebuildResourceList(UAtlasSubsystem& Atlas)
	{
		if (!ResourceList)
		{
			return;
		}
		const FAtlasStyle& Style = FAtlasStyle::Get();
		ResourceList->ClearChildren();

		const TArray<UAtlasSubsystem::FResourceKind>& Kinds = Atlas.GetResourceKinds();
		TArray<int32> Order;
		for (int32 i = 0; i < Kinds.Num(); i++)
		{
			if (Kinds[i].Count > 0)
			{
				Order.Add(i);
			}
		}
		Order.Sort([&Kinds](int32 A, int32 B)
		{
			return Kinds[A].bWell != Kinds[B].bWell ? !Kinds[A].bWell : Kinds[A].Name.CompareTo(Kinds[B].Name) < 0;
		});

		for (const int32 Index : Order)
		{
			const UAtlasSubsystem::FResourceKind& Kind = Kinds[Index];
			UClass* Descriptor = Kind.Descriptor;
			const EFeature Feature = Kind.bWell ? EFeature::ResourceWells : EFeature::ResourceNodes;
			const FString Label = Kind.bWell ? FString::Printf(TEXT("%s well"), *Kind.Name.ToString()) : Kind.Name.ToString();
			ResourceList->AddSlot().AutoHeight().Padding(FMargin(22.f, 1.f, 2.f, 1.f))
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda([Feature]() { return Sub() && Sub()->IsFeatureShown(Feature) ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
				[
					MakeToggle(
						[Descriptor]() { return Sub() && !Sub()->IsResourceHidden(Descriptor); },
						[Descriptor]() { if (Sub()) { Sub()->SetResourceHidden(Descriptor, !Sub()->IsResourceHidden(Descriptor)); } })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
				[
					SNew(SBox).WidthOverride(8.f).HeightOverride(8.f)
					[
						SNew(SImage).Image(&Style.Dot).ColorAndOpacity(FSlateColor(FLinearColor(Kind.Color)))
					]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[
					MakeLabel(FText::FromString(Label), Style.FontSmall, TextMain)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					MakeLabel(FText::AsNumber(Kind.Count), Style.FontSmall, TextDim)
				]
			];
		}
	}

	TSharedRef<SWidget> MakeCollectibleRow(ECollectible Kind, int32 Marker)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		TSharedRef<SHorizontalBox> Dots = SNew(SHorizontalBox);
		// Power slugs come in three tiers, each with its own dot colour.
		const int32 LastMarker = Kind == ECollectible::PowerSlug ? MarkerSlugPurple : Marker;
		for (int32 Each = Marker; Each <= LastMarker; Each++)
		{
			Dots->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 3.f, 0.f))
			[
				SNew(SBox).WidthOverride(8.f).HeightOverride(8.f)
				[
					SNew(SImage).Image(&Style.Dot).ColorAndOpacity(FSlateColor(FLinearColor(MarkerColor(Each))))
				]
			];
		}

		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 3.f, 8.f, 3.f))
			[
				MakeToggle(
					[Kind]() { return Sub() && Sub()->IsCollectibleShown(Kind); },
					[Kind]() { if (Sub()) { Sub()->SetCollectibleShown(Kind, !Sub()->IsCollectibleShown(Kind)); } })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 5.f, 0.f))
			[
				Dots
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				MakeLabel(CollectibleName(Kind), Style.Font, TextMain)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim))
				.Text_Lambda([Kind]()
				{
					return Sub() && Sub()->IsCollectibleShown(Kind) ? FText::AsNumber(Sub()->GetCollectibleCount(Kind)) : FText::GetEmpty();
				})
			];
	}

	TSharedRef<SWidget> MakeExportSizeButton(const TCHAR* Label, int32 Size)
	{
		return MakeTextButton(FText::FromString(Label),
			[Size]() { return Sub() && Sub()->GetSettings().ExportSize == Size; },
			[Size]()
			{
				if (UAtlasSubsystem* Atlas = Sub())
				{
					Atlas->EditSettings().ExportSize = Size;
					Atlas->SaveSettings();
				}
			});
	}

	TSharedRef<SWidget> MakeSettingsTab()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const auto Heading = [&Style](const TCHAR* Text) { return MakeLabel(Text, Style.FontSmall, TextDim); };

		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))[Heading(TEXT("Hover"))]
			+ SVerticalBox::Slot().AutoHeight()[MakeSettingRow(TEXT("Show building label"), &FSettings::bHoverLabel)]
			+ SVerticalBox::Slot().AutoHeight()[MakeSettingRow(TEXT("Show height"), &FSettings::bHoverHeight)]
			+ SVerticalBox::Slot().AutoHeight()[MakeSettingRow(TEXT("Show coordinates"), &FSettings::bHoverCoordinates)]
			+ SVerticalBox::Slot().AutoHeight()[MakeSettingRow(TEXT("Highlight hovered building"), &FSettings::bHighlight)]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 2.f))[Heading(TEXT("Map"))]
			+ SVerticalBox::Slot().AutoHeight()[MakeSettingRow(TEXT("Building outlines"), &FSettings::bOutlines)]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 2.f))
			[
				SNew(STextBlock).Font(Style.Font).ColorAndOpacity(FSlateColor(TextMain))
				.Text_Lambda([]() { return FText::FromString(FString::Printf(TEXT("Opacity  %d%%"), FMath::RoundToInt32((Sub() ? Sub()->GetSettings().Opacity : 1.f) * 100.f))); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SAtlasSlider)
				.Style(&Style.Slider)
				.IsFocusable(false)
				.Value_Lambda([]() { return Sub() ? (Sub()->GetSettings().Opacity - 0.2f) / 0.8f : 1.f; })
				.OnValueChanged_Lambda([](float Value) { if (Sub()) { Sub()->EditSettings().Opacity = 0.2f + Value * 0.8f; } })
				.OnMouseCaptureEnd_Lambda([]() { if (Sub()) { Sub()->SaveSettings(); } })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 3.f))[Heading(TEXT("Zoom limit"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 3.f, 0.f))[MakeZoomButton(TEXT("Game"), 1)]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 3.f, 0.f))[MakeZoomButton(TEXT("2x"), 2)]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 3.f, 0.f))[MakeZoomButton(TEXT("4x"), 4)]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 0.f, 0.f))[MakeZoomButton(TEXT("8x"), 8)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 3.f))[Heading(TEXT("Map image"))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 3.f, 0.f))[MakeExportSizeButton(TEXT("4096 px"), 4096)]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 0.f, 0.f))[MakeExportSizeButton(TEXT("8192 px"), 8192)]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				MakeTextButton(FText::FromString(TEXT("Export map image")),
					[]() { return false; },
					[]() { if (Sub()) { Sub()->StartExport(); } })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
			[
				SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)
				.Text_Lambda([]() { return Sub() ? Sub()->GetExportStatus() : FText::GetEmpty(); })
				.Visibility_Lambda([]() { return Sub() && !Sub()->GetExportStatus().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 10.f, 0.f, 0.f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 3.f, 0.f))
				[
					MakeTextButton(FText::FromString(TEXT("Reset settings")),
						[]() { return false; },
						[]() { if (Sub()) { Sub()->ResetSettings(); } })
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 0.f, 0.f))
				[
					MakeTextButton(FText::FromString(TEXT("Reset colors")),
						[]() { return false; },
						[this]() { if (Sub()) { Sub()->ResetColors(); } bListDirty = true; })
				]
			];
	}

	TSharedRef<SWidget> MakeZoomButton(const TCHAR* Label, int32 Factor)
	{
		return MakeTextButton(FText::FromString(Label),
			[Factor]() { return Sub() && Sub()->GetSettings().ZoomFactor == Factor; },
			[Factor]()
			{
				if (UAtlasSubsystem* Atlas = Sub())
				{
					Atlas->EditSettings().ZoomFactor = Factor;
					Atlas->SaveSettings();
				}
			});
	}

	TSharedRef<SWidget> MakeModeButton(const TCHAR* Label, bool bPaint)
	{
		return MakeTextButton(FText::FromString(Label),
			[bPaint]() { return Sub() && Sub()->IsPaintMode() == bPaint; },
			[bPaint]()
			{
				if (UAtlasSubsystem* Atlas = Sub())
				{
					Atlas->SetPaintMode(bPaint);
					Atlas->OnMapOpened();
				}
			});
	}

	float ValueToMetres(float Value) const
	{
		const FVector2f Bounds = Sub() ? Sub()->GetHeightBounds() : FVector2f(0.f, 1.f);
		return FMath::Lerp(Bounds.X, Bounds.Y, Value);
	}

	void ApplyHeight() const
	{
		if (UAtlasSubsystem* Atlas = Sub())
		{
			// At the ends of the track the filter is off, so new buildings beyond the old range still show.
			const float Low = HeightSlider->GetLow();
			const float High = HeightSlider->GetHigh();
			Atlas->SetHeightFilter(Low <= 0.f ? MinHeightMetres : ValueToMetres(Low), High >= 1.f ? MaxHeightMetres : ValueToMetres(High));
		}
	}

	FText HeightText() const
	{
		return FText::FromString(FString::Printf(TEXT("Height  %d m to %d m"),
			FMath::RoundToInt32(ValueToMetres(HeightSlider ? HeightSlider->GetLow() : 0.f)),
			FMath::RoundToInt32(ValueToMetres(HeightSlider ? HeightSlider->GetHigh() : 1.f))));
	}

	/** Building types use their own index, categories sit above every possible type index. */
	static int32 CategoryTarget(ECategory Category)
	{
		return MaxTypes + int32(Category);
	}

	TSharedRef<SWidget> MakeSwatch(TFunction<FColor()> GetColor, int32 Target)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SAtlasButton)
			.ButtonStyle(&Style.Flat)
			.IsFocusable(false)
			.ContentPadding(FMargin(0.f))
			.ToolTipText(FText::FromString(TEXT("Change color")))
			.OnClicked_Lambda([this, Target]()
			{
				PickerTarget = PickerTarget == Target ? INDEX_NONE : Target;
				bListDirty = true;
				return FReply::Handled();
			})
			[
				SNew(SBox).WidthOverride(14.f).HeightOverride(14.f)
				[
					SNew(SImage).Image(&Style.White)
					.ColorAndOpacity_Lambda([GetColor]() { return FSlateColor(FLinearColor(GetColor())); })
				]
			];
	}

	void ApplyColor(int32 Target, TOptional<FColor> Color)
	{
		if (UAtlasSubsystem* Atlas = Sub())
		{
			if (Target >= MaxTypes)
			{
				Atlas->SetCategoryColor(ECategory(Target - MaxTypes), Color);
			}
			else
			{
				Atlas->SetTypeColor(Target, Color);
			}
		}
		PickerTarget = INDEX_NONE;
		bListDirty = true;
	}

	TSharedRef<SWidget> MakePicker(int32 Target)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		constexpr int32 PerRow = 8;
		for (int32 Row = 0; Row * PerRow < UE_ARRAY_COUNT(ColorChoices); Row++)
		{
			TSharedRef<SHorizontalBox> Line = SNew(SHorizontalBox);
			for (int32 i = Row * PerRow; i < FMath::Min((Row + 1) * PerRow, int32(UE_ARRAY_COUNT(ColorChoices))); i++)
			{
				const FColor Choice = ColorChoices[i];
				Line->AddSlot().AutoWidth().Padding(FMargin(0.f, 0.f, 4.f, 4.f))
				[
					SNew(SAtlasButton)
					.ButtonStyle(&Style.Tinted)
					.IsFocusable(false)
					.ButtonColorAndOpacity(FSlateColor(FLinearColor(Choice)))
					.ContentPadding(FMargin(0.f))
					.OnClicked_Lambda([this, Target, Choice]() { ApplyColor(Target, Choice); return FReply::Handled(); })
					[
						SNew(SBox).WidthOverride(18.f).HeightOverride(18.f)
					]
				];
			}
			Rows->AddSlot().AutoHeight()[Line];
		}
		Rows->AddSlot().AutoHeight().HAlign(HAlign_Left)
		[
			SNew(SBox).WidthOverride(120.f)
			[
				MakeTextButton(FText::FromString(TEXT("Default color")), []() { return false; },
					[this, Target]() { ApplyColor(Target, TOptional<FColor>()); })
			]
		];
		return Rows;
	}

	void RebuildList(UAtlasSubsystem& Atlas)
	{
		UE_LOG(LogFactoryAtlas, Display, TEXT("[frame %llu] Layer list rebuilt"), GFrameCounter);
		if (!List)
		{
			return;
		}
		const FAtlasStyle& Style = FAtlasStyle::Get();
		List->ClearChildren();
		RebuildResourceList(Atlas);

		const TArray<FType>& Types = Atlas.GetTypes();
		for (int32 CategoryIndex = 0; CategoryIndex < int32(ECategory::Count); CategoryIndex++)
		{
			const ECategory Category = ECategory(CategoryIndex);
			TArray<int32> Members;
			int64 Total = 0;
			for (int32 i = 0; i < Types.Num(); i++)
			{
				if (Types[i].Category == Category && Types[i].Count > 0 && Types[i].Kind != EKind::Skip)
				{
					Members.Add(i);
					Total += Types[i].Count;
				}
			}
			if (Members.IsEmpty())
			{
				continue;
			}

			const bool bExpanded = Expanded.Contains(CategoryIndex);
			List->AddSlot().AutoHeight().Padding(FMargin(0.f, 1.f))
			[
				SNew(SAtlasButton)
				.ButtonStyle(&Style.Flat)
				.IsFocusable(false)
				.ContentPadding(FMargin(2.f, 2.f))
				.OnClicked_Lambda([this, CategoryIndex]()
				{
					if (!Expanded.Remove(CategoryIndex))
					{
						Expanded.Add(CategoryIndex);
					}
					bListDirty = true;
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						MakeToggle(
							[Category]() { return Sub() && !Sub()->IsCategoryHidden(Category); },
							[Category]() { if (Sub()) { Sub()->SetCategoryHidden(Category, !Sub()->IsCategoryHidden(Category)); } })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						MakeSwatch([Category]() { return Sub() ? Sub()->GetCategoryDisplayColor(Category) : CategoryColor(Category); }, CategoryTarget(Category))
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						MakeLabel(CategoryName(Category), Style.Font, TextMain)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeLabel(FText::AsNumber(Total), Style.FontSmall, TextDim)
					]
				]
			];

			if (PickerTarget == CategoryTarget(Category))
			{
				List->AddSlot().AutoHeight().Padding(FMargin(20.f, 2.f, 0.f, 4.f))[MakePicker(PickerTarget)];
			}

			if (!bExpanded)
			{
				continue;
			}

			Members.Sort([&Types](int32 A, int32 B) { return Types[A].Name.CompareTo(Types[B].Name) < 0; });
			for (const int32 TypeId : Members)
			{
				List->AddSlot().AutoHeight().Padding(FMargin(26.f, 1.f, 2.f, 1.f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						MakeToggle(
							[TypeId]() { return Sub() && Sub()->GetTypes().IsValidIndex(TypeId) && !Sub()->GetTypes()[TypeId].bHidden; },
							[TypeId]() { if (Sub() && Sub()->GetTypes().IsValidIndex(TypeId)) { Sub()->SetTypeHidden(TypeId, !Sub()->GetTypes()[TypeId].bHidden); } })
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))
					[
						MakeSwatch([TypeId]()
						{
							return Sub() && Sub()->GetTypes().IsValidIndex(TypeId) ? Sub()->GetTypes()[TypeId].Color : FColor::White;
						}, TypeId)
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						MakeLabel(Types[TypeId].Name, Style.FontSmall, TextMain)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeLabel(FText::AsNumber(Types[TypeId].Count), Style.FontSmall, TextDim)
					]
				];

				if (PickerTarget == TypeId)
				{
					List->AddSlot().AutoHeight().Padding(FMargin(46.f, 2.f, 0.f, 4.f))[MakePicker(PickerTarget)];
				}
			}
		}
	}

	TSharedPtr<SAtlasPanelBorder> Panel;
	TSharedPtr<SAtlasPanelBorder> SelectionPanel;
	FFactoryState State;
	TOptional<float> ClockDraft;
	TSharedPtr<SVerticalBox> RecipeList;
	bool bRecipePicker = false;
	TSharedPtr<SVerticalBox> List;
	TSharedPtr<SVerticalBox> ResourceList;
	TSharedPtr<SAtlasRangeSlider> HeightSlider;
	TSet<int32> Expanded;
	int32 PickerTarget = INDEX_NONE;
	bool bListDirty = true;
	bool bSettingsTab = false;
	FDelegateHandle FocusHandle;

public:
	virtual ~SAtlasHud() override
	{
		if (FocusHandle.IsValid() && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().OnFocusChanging().Remove(FocusHandle);
		}
	}

	/** Diagnostics for the menu flash: every focus change while the map is open goes to the log. */
	void WatchFocus()
	{
		FocusHandle = FSlateApplication::Get().OnFocusChanging().AddLambda(
			[](const FFocusEvent& Event, const FWeakWidgetPath&, const TSharedPtr<SWidget>& Old, const FWidgetPath&, const TSharedPtr<SWidget>& New)
			{
				UE_LOG(LogFactoryAtlas, Display, TEXT("[frame %llu] Focus %s -> %s"), GFrameCounter,
					Old ? *Old->GetTypeAsString() : TEXT("none"), New ? *New->GetTypeAsString() : TEXT("none"));
			});
	}
};

UAtlasMapHud::UAtlasMapHud(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetVisibilityInternal(ESlateVisibility::SelfHitTestInvisible);
}

/** Sees every click before any widget does, without consuming it, so map panning and markers keep working. */
class FAtlasClickProcessor : public IInputProcessor
{
public:
	TWeakPtr<SAtlasHud> Hud;

	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication&, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			DownAt = FVector2f(Event.GetScreenSpacePosition());
			bDown = true;
		}
		return false;
	}

	virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& Event) override
	{
		if (Event.GetEffectingButton() == EKeys::LeftMouseButton && bDown)
		{
			bDown = false;
			const FVector2f UpAt(Event.GetScreenSpacePosition());
			// A drag pans the map; only a click that stayed put selects.
			if (FVector2f::Distance(UpAt, DownAt) < 5.f)
			{
				if (const TSharedPtr<SAtlasHud> Pinned = Hud.Pin())
				{
					Pinned->OnMapClick(UpAt);
				}
			}
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("AtlasClick"); }

private:
	FVector2f DownAt = FVector2f::ZeroVector;
	bool bDown = false;
};

TSharedRef<SWidget> UAtlasMapHud::RebuildWidget()
{
	Super::RebuildWidget();
	UE_LOG(LogFactoryAtlas, Display, TEXT("[frame %llu] Menu built"), GFrameCounter);
	Hud = SNew(SAtlasHud);
	Hud->WatchFocus();

	if (FSlateApplication::IsInitialized())
	{
		if (ClickProcessor)
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(ClickProcessor);
		}
		TSharedRef<FAtlasClickProcessor> Processor = MakeShared<FAtlasClickProcessor>();
		Processor->Hud = Hud;
		ClickProcessor = Processor;
		FSlateApplication::Get().RegisterInputPreProcessor(ClickProcessor);
	}
	return Hud.ToSharedRef();
}

void UAtlasMapHud::ReleaseSlateResources(bool bReleaseChildren)
{
	UE_LOG(LogFactoryAtlas, Display, TEXT("[frame %llu] Menu released"), GFrameCounter);
	if (ClickProcessor && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(ClickProcessor);
	}
	ClickProcessor.Reset();
	if (UAtlasSubsystem* Atlas = Sub())
	{
		Atlas->ClearSelection();
	}
	Super::ReleaseSlateResources(bReleaseChildren);
	Hud.Reset();
}
