#include "AtlasMapHud.h"

#include "AtlasMapWindow.h"
#include "AtlasSubsystem.h"
#include "FactoryAtlas.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/BorderSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Containers/Ticker.h"
#include "Components/GridSlot.h"
#include "Components/SafeZoneSlot.h"
#include "Blueprint/GameViewportSubsystem.h"
#include "Engine/GameViewportClient.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Engine.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SToolTip.h"
#include "Widgets/SViewport.h"
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
		// Tooltips, in the look of the game's key hints: a dark tile with an orange key chip.
		FSlateRoundedBoxBrush Tip{ FLinearColor(FColor(22, 23, 26, 225)), 2.f };
		FSlateRoundedBoxBrush KeyChip{ Accent, 2.f };
		FSlateColorBrush TextSelection{ FLinearColor(Accent.R, Accent.G, Accent.B, 0.45f) };
		FEditableTextStyle TextInput;

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

			TextInput.SetFont(Font).SetColorAndOpacity(FSlateColor(TextMain))
				.SetBackgroundImageSelected(TextSelection).SetBackgroundImageComposing(TextSelection).SetCaretImage(White);

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

	/** A plain tooltip as a dark tile, instead of Slate's stock look. */
	TSharedPtr<IToolTip> MakeTip(const FText& Text)
	{
		if (Text.IsEmpty())
		{
			return nullptr; // A custom tooltip would show as an empty tile
		}
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SToolTip)
			.BorderImage(&Style.Clear)
			.TextMargin(FMargin(0.f))
			[
				SNew(SBorder)
				.BorderImage(&Style.Tip)
				.Padding(FMargin(10.f, 6.f))
				[
					SNew(STextBlock).Font(Style.Font).ColorAndOpacity(FSlateColor(TextMain)).WrapTextAt(300.f).Text(Text)
				]
			];
	}

	/**
	 * The map's mouse shortcuts along the bottom of the map, in the look of the game's own key hints: each a dark
	 * tile with its action over an orange key chip. Clicks pass through it to the map.
	 */
	TSharedRef<SWidget> MakeKeyHints(const TArray<TPair<const TCHAR*, const TCHAR*>>& Shortcuts);

	TSharedRef<SWidget> MakeShortcutBar(EView View)
	{
		TArray<TPair<const TCHAR*, const TCHAR*>> Shortcuts = {
			{ TEXT("Open Settings"), TEXT("LMB") },
			{ TEXT("Copy Settings"), TEXT("Ctrl + RMB") },
			{ TEXT("Paste Settings"), TEXT("Ctrl + LMB") },
			{ TEXT("Paste Along"), TEXT("Ctrl + Drag LMB") },
			{ TEXT("Build Belt, Pipe or Track"), TEXT("Drag LMB from Port") },
			{ TEXT("Close Settings"), TEXT("LMB Empty Map") },
			{ TEXT("Zoom"), TEXT("Scroll") },
		};
		if (View == EView::Window)
		{
			// The game's map pans itself; the window's map is panned here.
			Shortcuts.Add({ TEXT("Pan"), TEXT("Drag LMB / RMB") });
			Shortcuts.Add({ TEXT("Find Yourself"), TEXT("Double-click") });
		}
		return MakeKeyHints(Shortcuts);
	}

	/** A row of key hint tiles, each an action over an orange key chip. */
	TSharedRef<SWidget> MakeKeyHints(const TArray<TPair<const TCHAR*, const TCHAR*>>& Shortcuts)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox).Visibility(EVisibility::HitTestInvisible);
		for (int32 i = 0; i < Shortcuts.Num(); i++)
		{
			Bar->AddSlot().AutoWidth().Padding(FMargin(2.f, 0.f))
			[
				SNew(SBorder)
				.BorderImage(&Style.Tip)
				.Padding(FMargin(12.f, 6.f, 12.f, 5.f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
					[
						SNew(STextBlock).Font(Style.FontBold).ColorAndOpacity(FSlateColor(TextMain)).Text(FText::FromString(Shortcuts[i].Key))
					]
					+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 0.f))
					[
						SNew(SBorder)
						.BorderImage(&Style.KeyChip)
						.Padding(FMargin(6.f, 0.f))
						[
							SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(FLinearColor::White)).Text(FText::FromString(Shortcuts[i].Value))
						]
					]
				]
			];
		}
		return Bar;
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
			FSlateApplication& Slate = FSlateApplication::Get();
			if (const TSharedPtr<SWidget> Focused = Slate.GetUserFocusedWidget(Event.GetUserIndex()))
			{
				// Only within the window clicked: in the separate map window, the focused widget is usually in the
				// game's window, and naming it would hand focus back to the game on every click.
				const FWidgetPath Clicked = Slate.LocateWindowUnderMouse(Event.GetScreenSpacePosition(), Slate.GetInteractiveTopLevelWindows());
				if (Clicked.IsValid() && Slate.FindWidgetWindow(Focused.ToSharedRef()) == Clicked.GetWindow())
				{
					Reply.SetUserFocus(Focused.ToSharedRef(), EFocusCause::Mouse);
				}
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

	// The game stores colors linear; people pick them in sRGB, so the picker's hue, saturation and value are sRGB.
	FVector3f ToHsv(const FLinearColor& Linear)
	{
		const FColor Srgb = Linear.ToFColorSRGB();
		const FLinearColor Hsv = FLinearColor(Srgb.R / 255.f, Srgb.G / 255.f, Srgb.B / 255.f).LinearRGBToHSV();
		return FVector3f(Hsv.R, Hsv.G, Hsv.B);
	}

	FLinearColor FromHsv(const FVector3f& Hsv)
	{
		const FLinearColor Rgb = FLinearColor(Hsv.X, Hsv.Y, Hsv.Z).HSVToLinearRGB();
		return FLinearColor::FromSRGBColor(FColor(uint8(FMath::RoundToInt32(FMath::Clamp(Rgb.R, 0.f, 1.f) * 255.f)),
			uint8(FMath::RoundToInt32(FMath::Clamp(Rgb.G, 0.f, 1.f) * 255.f)), uint8(FMath::RoundToInt32(FMath::Clamp(Rgb.B, 0.f, 1.f) * 255.f))));
	}

	FString ToHex(const FLinearColor& Linear)
	{
		const FColor Srgb = Linear.ToFColorSRGB();
		return FString::Printf(TEXT("%02X%02X%02X"), Srgb.R, Srgb.G, Srgb.B);
	}

	/** "#FF8800" or "ff8800". FColor::FromHex alone accepts malformed text, so this checks first. */
	bool FromHex(const FString& Text, FLinearColor& Out)
	{
		FString Hex = Text.TrimStartAndEnd();
		Hex.RemoveFromStart(TEXT("#"));
		if (Hex.Len() != 6)
		{
			return false;
		}
		for (const TCHAR Ch : Hex)
		{
			if (!FChar::IsHexDigit(Ch))
			{
				return false;
			}
		}
		Out = FLinearColor::FromSRGBColor(FColor::FromHex(Hex));
		return true;
	}

	/** The six-stop hue rainbow, left to right across Size. */
	void PaintHue(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FVector2f& Position, const FVector2f& Size)
	{
		TArray<FSlateGradientStop> Stops;
		for (int32 i = 0; i <= 6; i++)
		{
			// 359.9 rather than 360 for the last stop: HSVToLinearRGB expects hues below 360.
			Stops.Add(FSlateGradientStop(FVector2f(Size.X * i / 6.f, 0.f), FLinearColor(FMath::Min(i * 60.f, 359.9f), 1.f, 1.f).HSVToLinearRGB()));
		}
		// Orient_Vertical: the stops run along X, so the bands are vertical.
		FSlateDrawElement::MakeGradient(Elements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), MoveTemp(Stops), Orient_Vertical);
	}

	/** A small rainbow square, the face of the "custom color" choice. */
	class SAtlasHueChip : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAtlasHueChip) {}
		SLATE_END_ARGS()

		void Construct(const FArguments&) {}

		virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(22.0, 22.0); }

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
			int32 LayerId, const FWidgetStyle&, bool) const override
		{
			const FVector2f Size(Geometry.GetLocalSize());
			PaintHue(Elements, LayerId, Geometry, FVector2f::ZeroVector, Size);
			// Fades to white at the top, as a hint that any shade can be picked.
			TArray<FSlateGradientStop> Fade = { FSlateGradientStop(FVector2f(0.f, 0.f), FLinearColor(1.f, 1.f, 1.f, 0.8f)),
				FSlateGradientStop(FVector2f(0.f, Size.Y), FLinearColor(1.f, 1.f, 1.f, 0.f)) };
			FSlateDrawElement::MakeGradient(Elements, LayerId + 1, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform()), MoveTemp(Fade), Orient_Horizontal);
			return LayerId + 1;
		}
	};

	/**
	 * Saturation across and brightness down in a square of the current hue, with the hue bar beneath it.
	 * Reports every change while dragging and commits once on release, so the building is repainted once per drag.
	 */
	class SAtlasColorField : public SLeafWidget
	{
	public:
		DECLARE_DELEGATE_OneParam(FOnColor, FLinearColor);

		SLATE_BEGIN_ARGS(SAtlasColorField) {}
			SLATE_ATTRIBUTE(FLinearColor, Color)
			SLATE_EVENT(FOnColor, OnChanged)
			SLATE_EVENT(FSimpleDelegate, OnCommitted)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Color = InArgs._Color;
			OnChanged = InArgs._OnChanged;
			OnCommitted = InArgs._OnCommitted;
		}

		bool IsDragging() const { return HasMouseCapture(); }

		virtual FVector2D ComputeDesiredSize(float) const override
		{
			return FVector2D(200.0, SquareHeight + Gap + HueHeight);
		}

		virtual void Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime) override
		{
			if (HasMouseCapture())
			{
				return;
			}
			// A grey has no hue, so keep the last one rather than jumping back to red.
			const FVector3f Now = ToHsv(Color.Get());
			Hsv = FVector3f(Now.Y > 0.001f && Now.Z > 0.001f ? Now.X : Hsv.X, Now.Y, Now.Z);
		}

		virtual int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
			int32 LayerId, const FWidgetStyle&, bool bParentEnabled) const override
		{
			const FAtlasStyle& Style = FAtlasStyle::Get();
			const float Width = float(Geometry.GetLocalSize().X);
			const FVector2f Square(Width, SquareHeight);
			const auto Box = [&](int32 Layer, const FVector2f& Position, const FVector2f& Size, const FLinearColor& Tint)
			{
				FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), &Style.White, ESlateDrawEffect::None, Tint);
			};

			// The hue at full strength, washed to white towards the left and darkened to black towards the bottom.
			Box(LayerId, FVector2f::ZeroVector, Square, FLinearColor(Hsv.X, 1.f, 1.f).HSVToLinearRGB());
			TArray<FSlateGradientStop> ToWhite = { FSlateGradientStop(FVector2f(0.f, 0.f), FLinearColor::White),
				FSlateGradientStop(FVector2f(Width, 0.f), FLinearColor(1.f, 1.f, 1.f, 0.f)) };
			FSlateDrawElement::MakeGradient(Elements, LayerId + 1, Geometry.ToPaintGeometry(Square, FSlateLayoutTransform()), MoveTemp(ToWhite), Orient_Vertical);
			TArray<FSlateGradientStop> ToBlack = { FSlateGradientStop(FVector2f(0.f, 0.f), FLinearColor(0.f, 0.f, 0.f, 0.f)),
				FSlateGradientStop(FVector2f(0.f, SquareHeight), FLinearColor::Black) };
			FSlateDrawElement::MakeGradient(Elements, LayerId + 2, Geometry.ToPaintGeometry(Square, FSlateLayoutTransform()), MoveTemp(ToBlack), Orient_Horizontal);

			const float HueY = SquareHeight + Gap;
			PaintHue(Elements, LayerId, Geometry, FVector2f(0.f, HueY), FVector2f(Width, HueHeight));

			// Markers: a ring at the picked shade, a line at the picked hue.
			const FVector2f Spot(Hsv.Y * Width, (1.f - Hsv.Z) * SquareHeight);
			Box(LayerId + 3, Spot - FVector2f(5.f), FVector2f(10.f), FLinearColor::White);
			Box(LayerId + 4, Spot - FVector2f(3.f), FVector2f(6.f), FromHsv(Hsv));
			const float HueX = Hsv.X / 360.f * Width;
			Box(LayerId + 3, FVector2f(HueX - 2.f, HueY - 2.f), FVector2f(4.f, HueHeight + 4.f), FLinearColor::White);

			if (!bParentEnabled)
			{
				Box(LayerId + 5, FVector2f::ZeroVector, FVector2f(Geometry.GetLocalSize()), FLinearColor(BodyColor.R, BodyColor.G, BodyColor.B, 0.6f));
			}
			return LayerId + 5;
		}

		virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
			{
				return FReply::Unhandled();
			}
			const FVector2f Local(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
			bDraggingHue = Local.Y > SquareHeight + Gap * 0.5f;
			Drag(Geometry, Event);
			return KeepFocus(FReply::Handled().CaptureMouse(SharedThis(this)), Event);
		}

		virtual FReply OnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override
		{
			if (!HasMouseCapture())
			{
				return FReply::Unhandled();
			}
			Drag(Geometry, Event);
			return FReply::Handled();
		}

		virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override
		{
			if (!HasMouseCapture())
			{
				return FReply::Unhandled();
			}
			OnCommitted.ExecuteIfBound();
			return FReply::Handled().ReleaseMouseCapture();
		}

	private:
		static constexpr float SquareHeight = 96.f;
		static constexpr float Gap = 6.f;
		static constexpr float HueHeight = 12.f;

		void Drag(const FGeometry& Geometry, const FPointerEvent& Event)
		{
			const FVector2f Local(Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
			const float Across = FMath::Clamp(Local.X / FMath::Max(float(Geometry.GetLocalSize().X), 1.f), 0.f, 1.f);
			if (bDraggingHue)
			{
				Hsv.X = FMath::Min(Across * 360.f, 359.9f);
			}
			else
			{
				Hsv.Y = Across;
				Hsv.Z = 1.f - FMath::Clamp(Local.Y / SquareHeight, 0.f, 1.f);
			}
			Invalidate(EInvalidateWidgetReason::Paint);
			OnChanged.ExecuteIfBound(FromHsv(Hsv));
		}

		TAttribute<FLinearColor> Color;
		FOnColor OnChanged;
		FSimpleDelegate OnCommitted;
		FVector3f Hsv = FVector3f(0.f, 0.f, 1.f);
		bool bDraggingHue = false;
	};
}

class SAtlasHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SAtlasHud) : _View(EView::Game) {}
		/** The game's map screen, or the separate map window. */
		SLATE_ARGUMENT(EView, View)
	SLATE_END_ARGS()

	/** The UMG widget hosting this one, which owns the map screen changes fullscreen makes. Unset in the window. */
	TWeakObjectPtr<UAtlasMapHud> Owner;

	/** In the window: the click processor feeding this HUD, removed with it. */
	TSharedPtr<IInputProcessor> OwnedProcessor;

	void Construct(const FArguments& InArgs)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		View = InArgs._View;
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
			// Kept above the screen's bottom edge even when the map reaches past it; see HintInset.
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Bottom)
			// 106 clears the game's own marker and stamp bar along the bottom of the map, about 90 tall.
			.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(0.f, 0.f, 0.f, View == EView::Game ? HintInset + 106.f : 12.f); }))
			[
				SNew(SHorizontalBox)
				.Visibility(EVisibility::SelfHitTestInvisible)
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(2.f, 0.f))
				[
					SAssignNew(MeasureTile, SBox)[MakeMeasureTile()]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(2.f, 0.f))
				[
					// Only where wires can be built from the map: single player and the host.
					SAssignNew(WireTile, SBox)
					.Visibility_Lambda([this]() { const UAtlasSubsystem* Atlas = Sub(); return Atlas && Atlas->CanBuildBelts() ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						MakeModeTile(TEXT("Wire"), TEXT("Alt + W"),
							[this]() { const UAtlasSubsystem* Atlas = Sub(); return Atlas && Atlas->IsWiring(View); },
							[this]() { if (UAtlasSubsystem* Atlas = Sub()) { Atlas->SetWiring(View, !Atlas->IsWiring(View)); } })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { const UAtlasSubsystem* Atlas = Sub(); return Atlas && (Atlas->IsMeasuring(View) || Atlas->IsWiring(View)) ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
					[
						MakeShortcutBar(View)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { const UAtlasSubsystem* Atlas = Sub(); return Atlas && Atlas->IsMeasuring(View) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					[
						MakeKeyHints({
							{ TEXT("Measure Box"), TEXT("Drag LMB") },
							{ TEXT("Measure Line"), TEXT("Shift + Drag LMB") },
							{ TEXT("Pick Box"), TEXT("LMB on Box") },
							{ TEXT("Pan"), TEXT("Space + Drag LMB") },
							{ TEXT("Stop Measuring"), TEXT("Esc / G") },
						})
					]
				]
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.Visibility_Lambda([this]() { const UAtlasSubsystem* Atlas = Sub(); return Atlas && Atlas->IsWiring(View) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					[
						MakeKeyHints({
							{ TEXT("Connect"), TEXT("LMB Building") },
							{ TEXT("Place Pole"), TEXT("LMB Ground") },
							{ TEXT("Pan"), TEXT("Drag LMB") },
							{ TEXT("Drop Wire"), TEXT("RMB") },
							{ TEXT("Stop Wiring"), TEXT("Esc / Alt + W") },
						})
					]
				]
			]
			// Placed over the selected building each frame; see PlaceSelectionPanel.
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(SelectionOffset.X, SelectionOffset.Y, 0.f, 0.f); }))
			[
				MakeSelectionPanel()
			]
			// Below the active measure box while measuring; see PlaceMeasurePanel.
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(TAttribute<FMargin>::CreateLambda([this]() { return FMargin(MeasureOffset.X, MeasureOffset.Y, 0.f, 0.f); }))
			[
				MakeMeasurePanel()
			]
		];
	}

	// Measure tool
	bool HasMeasurePanel() const
	{
		const UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || !Atlas->IsMeasuring(View))
		{
			return false;
		}
		const FMeasureView& Measured = Atlas->GetMeasure(View);
		return Measured.Boxes.IsValidIndex(Measured.Active);
	}

	/** A left press on the map: true when measure mode is on here and the press landed on the map. */
	bool OnMapMeasurePress(const FVector2f& ScreenPosition, bool bLine)
	{
		UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || !Atlas->IsMeasuring(View) || Atlas->IsPointerOverPanel(View) || !Atlas->IsPointOnMap(ScreenPosition, View))
		{
			return false;
		}
		Atlas->PressMeasure(View, bLine);
		return true;
	}

	void OnMapMeasureMove()
	{
		if (UAtlasSubsystem* Atlas = Sub())
		{
			Atlas->MoveMeasure(View);
		}
	}

	void OnMapMeasureRelease()
	{
		if (UAtlasSubsystem* Atlas = Sub())
		{
			Atlas->ReleaseMeasure(View);
		}
	}

	void OnMapMeasureCancel()
	{
		if (UAtlasSubsystem* Atlas = Sub())
		{
			Atlas->CancelMeasureDrag(View);
		}
	}

	/** G toggles measure mode, Esc leaves it. Only for the map under the cursor, and never while typing. */
	bool OnMeasureKey(const FKeyEvent& Event)
	{
		UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || !GetTickSpaceGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos()))
		{
			return false;
		}
		if (const TSharedPtr<SWidget> Focused = FSlateApplication::Get().GetKeyboardFocusedWidget())
		{
			const FName Type = Focused->GetType();
			if (Type == FName(TEXT("SEditableText")) || Type == FName(TEXT("SMultiLineEditableText")))
			{
				return false;
			}
		}
		const FKey Key = Event.GetKey();
		// Alt+W toggles wire mode, like Factorio's wire shortcut. Esc lets go of the wire, then leaves wire mode.
		if (Key == EKeys::W && Event.IsAltDown() && !Event.IsControlDown() && !Event.IsShiftDown())
		{
			if (!Event.IsRepeat())
			{
				Atlas->SetWiring(View, !Atlas->IsWiring(View));
			}
			return true;
		}
		if (Key == EKeys::Escape && Atlas->IsWiring(View))
		{
			if (!Atlas->DropWire())
			{
				Atlas->SetWiring(View, false);
			}
			return true;
		}
		if (Key == EKeys::G && !Event.GetModifierKeys().AnyModifiersDown())
		{
			if (!Event.IsRepeat())
			{
				Atlas->SetMeasuring(View, !Atlas->IsMeasuring(View));
			}
			return true;
		}
		if (Key == EKeys::Escape && Atlas->IsMeasuring(View))
		{
			if (Atlas->GetMeasure(View).bPressed)
			{
				Atlas->CancelMeasureDrag(View);
			}
			else
			{
				Atlas->SetMeasuring(View, false);
			}
			return true;
		}
		return false;
	}

	/** The Measure toggle at the start of the shortcut bar, lit while measuring. */
	TSharedRef<SWidget> MakeMeasureTile()
	{
		return MakeModeTile(TEXT("Measure"), TEXT("G"),
			[this]() { const UAtlasSubsystem* Atlas = Sub(); return Atlas && Atlas->IsMeasuring(View); },
			[this]() { if (UAtlasSubsystem* Atlas = Sub()) { Atlas->SetMeasuring(View, !Atlas->IsMeasuring(View)); } });
	}

	/** A mode toggle on the shortcut bar, in the look of a key hint, lit while its mode is on. */
	TSharedRef<SWidget> MakeModeTile(const TCHAR* Label, const TCHAR* KeyName, TFunction<bool()> IsOn, TFunction<void()> Toggle)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SAtlasButton)
			.ButtonStyle(&Style.Tinted)
			.IsFocusable(false)
			.ContentPadding(FMargin(12.f, 6.f, 12.f, 5.f))
			.ButtonColorAndOpacity_Lambda([IsOn]() { return FSlateColor(IsOn() ? Accent : FLinearColor(FColor(22, 23, 26, 225))); })
			.OnClicked_Lambda([Toggle]()
			{
				Toggle();
				return FReply::Handled();
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					MakeLabel(Label, Style.FontBold, TextMain)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(FMargin(0.f, 4.f, 0.f, 0.f))
				[
					SNew(SBorder)
					.BorderImage(&Style.KeyChip)
					.BorderBackgroundColor_Lambda([IsOn]() { return FSlateColor(IsOn() ? FLinearColor(FColor(16, 16, 17)) : FLinearColor::White); })
					.Padding(FMargin(6.f, 0.f))
					[
						MakeLabel(KeyName, Style.FontSmall, FLinearColor::White)
					]
				]
			];
	}

	/** Copies, their direction, and removing boxes, for the active measure box. */
	TSharedRef<SWidget> MakeMeasurePanel()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const auto Edit = [this](TFunction<void(UAtlasSubsystem&)> Action)
		{
			return [this, Action]()
			{
				if (UAtlasSubsystem* Atlas = Sub())
				{
					Action(*Atlas);
				}
			};
		};
		const auto Copies = [this]()
		{
			const UAtlasSubsystem* Atlas = Sub();
			const FMeasureView* Measured = Atlas ? &Atlas->GetMeasure(View) : nullptr;
			return Measured && Measured->Boxes.IsValidIndex(Measured->Active) ? Measured->Boxes[Measured->Active].Copies : 0;
		};
		const auto DirectionIs = [this](EMeasureDirection Direction)
		{
			return [this, Direction]()
			{
				const UAtlasSubsystem* Atlas = Sub();
				const FMeasureView* Measured = Atlas ? &Atlas->GetMeasure(View) : nullptr;
				return Measured && Measured->Boxes.IsValidIndex(Measured->Active) && Measured->Boxes[Measured->Active].Direction == Direction;
			};
		};
		const auto Never = []() { return false; };
		const auto Button = [&](const TCHAR* Label, TFunction<bool()> IsActive, TFunction<void()> OnClick)
		{
			return SNew(SBox).MinDesiredWidth(26.f)[MakeTextButton(FText::FromString(Label), IsActive, OnClick)];
		};

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 6.f, 0.f))[MakeLabel(TEXT("Copies"), Style.Font, TextDim)];
		Row->AddSlot().AutoWidth().Padding(FMargin(1.f, 0.f))[Button(TEXT("-"), Never, Edit([this, Copies](UAtlasSubsystem& Atlas) { Atlas.SetMeasureCopies(View, Copies() - 1); }))];
		Row->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(6.f, 0.f))
		[
			SNew(SBox).MinDesiredWidth(16.f).HAlign(HAlign_Center)
			[
				SNew(STextBlock).Font(Style.FontBold).ColorAndOpacity(FSlateColor(TextMain)).Text_Lambda([Copies]() { return FText::AsNumber(Copies()); })
			]
		];
		Row->AddSlot().AutoWidth().Padding(FMargin(1.f, 0.f, 10.f, 0.f))[Button(TEXT("+"), Never, Edit([this, Copies](UAtlasSubsystem& Atlas) { Atlas.SetMeasureCopies(View, Copies() + 1); }))];
		const TPair<const TCHAR*, EMeasureDirection> Directions[] = {
			{ TEXT("Left"), EMeasureDirection::Left }, { TEXT("Up"), EMeasureDirection::Up },
			{ TEXT("Down"), EMeasureDirection::Down }, { TEXT("Right"), EMeasureDirection::Right },
		};
		for (const TPair<const TCHAR*, EMeasureDirection>& Direction : Directions)
		{
			const EMeasureDirection Value = Direction.Value;
			Row->AddSlot().AutoWidth().Padding(FMargin(1.f, 0.f))
			[
				Button(Direction.Key, DirectionIs(Value), Edit([this, Value](UAtlasSubsystem& Atlas) { Atlas.SetMeasureDirection(View, Value); }))
			];
		}
		Row->AddSlot().AutoWidth().Padding(FMargin(10.f, 0.f, 1.f, 0.f))[Button(TEXT("Remove"), Never, Edit([this](UAtlasSubsystem& Atlas) { Atlas.RemoveActiveMeasure(View); }))];
		Row->AddSlot().AutoWidth().Padding(FMargin(1.f, 0.f))[Button(TEXT("Clear All"), Never, Edit([this](UAtlasSubsystem& Atlas) { Atlas.ClearMeasures(View); }))];

		return SAssignNew(MeasurePanel, SAtlasPanelBorder)
			.BorderImage(&Style.Body)
			.Padding(FMargin(8.f, 6.f))
			.Visibility_Lambda([this]() { return HasMeasurePanel() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				Row
			];
	}

	/** Centres the measure panel under the active box, or over it when there is no room, kept on screen. */
	void PlaceMeasurePanel(const FGeometry& Geometry, const UAtlasSubsystem& Atlas)
	{
		if (!MeasurePanel.IsValid() || !HasMeasurePanel())
		{
			return;
		}
		constexpr float Gap = 12.f;
		constexpr float Edge = 4.f;
		const FMeasureView& Measured = Atlas.GetMeasure(View);
		const FMeasureBox& Box = Measured.Boxes[Measured.Active];
		FVector2f Corners[4];
		Box.Corners(0, Corners);
		const FSlateRenderTransform ToLocal = Geometry.GetAccumulatedRenderTransform().Inverse();
		FVector2f Low(TNumericLimits<float>::Max()), High(TNumericLimits<float>::Lowest());
		for (const FVector2f& Corner : Corners)
		{
			const FVector2f Local = ToLocal.TransformPoint(Atlas.MetresToPaint(View, Corner));
			Low = FVector2f::Min(Low, Local);
			High = FVector2f::Max(High, Local);
		}
		MeasurePanel->SlatePrepass(GetPrepassLayoutScaleMultiplier());
		const FVector2f PanelSize(MeasurePanel->GetDesiredSize());
		const FVector2f Bounds(Geometry.GetLocalSize());
		float X = (Low.X + High.X) * 0.5f - PanelSize.X * 0.5f;
		float Y = High.Y + Gap;
		if (Y + PanelSize.Y > Bounds.Y - Edge)
		{
			Y = Low.Y - Gap - PanelSize.Y;
		}
		X = FMath::Max(FMath::Min(X, Bounds.X - PanelSize.X - Edge), Edge);
		Y = FMath::Max(FMath::Min(Y, Bounds.Y - PanelSize.Y - Edge), Edge);
		MeasureOffset = FVector2f(FMath::RoundToFloat(X), FMath::RoundToFloat(Y));
	}

	static FString FormatMetres(float Value)
	{
		const float Rounded = FMath::RoundToFloat(Value);
		return FMath::IsNearlyEqual(Value, Rounded, 0.05f) ? FString::Printf(TEXT("%d"), int32(Rounded)) : FString::Printf(TEXT("%.1f"), Value);
	}

	/** A small label of a few lines, centred on Anchor and above it, kept inside the widget. */
	void PaintTag(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer, FVector2f Anchor,
		const TArray<TPair<FString, FLinearColor>>& Lines, const FLinearColor& Edge) const
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2f Padding(7.f, 3.f);
		float Width = 0.f;
		float LineHeight = 0.f;
		for (const TPair<FString, FLinearColor>& Line : Lines)
		{
			const FVector2f Size(Measure->Measure(Line.Key, Style.Font));
			Width = FMath::Max(Width, Size.X);
			LineHeight = Size.Y;
		}
		const FVector2f BoxSize(Width + Padding.X * 2.f, LineHeight * Lines.Num() + Padding.Y * 2.f);
		const FVector2f LocalSize(AllottedGeometry.GetLocalSize());
		FVector2f Position(Anchor.X - BoxSize.X * 0.5f, Anchor.Y - BoxSize.Y - 6.f);
		Position.X = FMath::Clamp(Position.X, 4.f, FMath::Max(4.f, LocalSize.X - BoxSize.X - 4.f));
		Position.Y = FMath::Clamp(Position.Y, 4.f, FMath::Max(4.f, LocalSize.Y - BoxSize.Y - 4.f));
		Position = FVector2f(FMath::RoundToFloat(Position.X), FMath::RoundToFloat(Position.Y));

		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, BodyColor);
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(FVector2f(3.f, BoxSize.Y), FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, Edge);
		FVector2f LinePosition = Position + Padding + FVector2f(2.f, 0.f);
		for (const TPair<FString, FLinearColor>& Line : Lines)
		{
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(FVector2f(Width, LineHeight), FSlateLayoutTransform(LinePosition)),
				Line.Key, Style.Font, ESlateDrawEffect::None, Line.Value);
			LinePosition.Y += LineHeight;
		}
	}

	/** Measure boxes, their copies, lines, and the one being dragged, with their sizes. */
	int32 PaintMeasure(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer) const
	{
		const UAtlasSubsystem* Atlas = Sub();
		if (!Atlas)
		{
			return Layer;
		}
		const FMeasureView& Measured = Atlas->GetMeasure(View);
		const bool bDraft = Measured.bPressed && Measured.bMoved;
		if (Measured.Boxes.IsEmpty() && Measured.Lines.IsEmpty() && !bDraft)
		{
			return Layer;
		}

		const FGeometry& Tick = GetTickSpaceGeometry();
		const auto ToLocal = [&](const FVector2f& Metres) { return FVector2f(Tick.AbsoluteToLocal(FVector2D(Atlas->MetresToPaint(View, Metres)))); };
		const FPaintGeometry Paint = AllottedGeometry.ToPaintGeometry();
		const FLinearColor Blocked(FColor(235, 64, 52));
		const FLinearColor Ghost(1.f, 1.f, 1.f, 0.7f);
		const int32 LineLayer = Layer + 1;
		const int32 TagLayer = Layer + 2;

		const auto Outline = [&](const FMeasureBox& Box, int32 Copy, const FLinearColor& Color, float Thickness)
		{
			FVector2f Corners[4];
			Box.Corners(Copy, Corners);
			TArray<FVector2f> Points;
			for (int32 i = 0; i <= 4; i++)
			{
				Points.Add(ToLocal(Corners[i % 4]));
			}
			FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, Paint, Points, ESlateDrawEffect::None, Color, true, Thickness);
			// The label goes over the highest point of the shape on screen.
			FVector2f Top = Points[0];
			float Left = Points[0].X, Right = Points[0].X;
			for (const FVector2f& Point : Points)
			{
				Top.Y = FMath::Min(Top.Y, Point.Y);
				Left = FMath::Min(Left, Point.X);
				Right = FMath::Max(Right, Point.X);
			}
			Top.X = (Left + Right) * 0.5f;
			return Top;
		};

		const auto BoxLines = [&](const FMeasureBox& Box, bool bStats)
		{
			TArray<TPair<FString, FLinearColor>> Lines;
			const FVector2f Span = Box.Size();
			Lines.Add({ FString::Printf(TEXT("%s x %s m"), *FormatMetres(Span.X), *FormatMetres(Span.Y)), TextMain });
			Lines.Add({ FString::Printf(TEXT("%s x %s foundations"), *FormatMetres(Span.X / 8.f), *FormatMetres(Span.Y / 8.f)), TextDim });
			if (bStats && Box.bHasHeight)
			{
				const int32 LowM = FMath::RoundToInt32(Box.LowMetres);
				const int32 HighM = FMath::RoundToInt32(Box.HighMetres);
				const FString Range = LowM == HighM ? FString::Printf(TEXT("at %d m"), LowM) : FString::Printf(TEXT("%d to %d m"), LowM, HighM);
				Lines.Add({ Box.Floors > 0 ? FString::Printf(TEXT("%d %s, %s"), Box.Floors, Box.Floors == 1 ? TEXT("floor") : TEXT("floors"), *Range) : Range, TextDim });
			}
			if (bStats && Box.Copies > 0)
			{
				int32 BlockedCount = 0;
				for (const bool bBlocked : Box.Blocked)
				{
					BlockedCount += bBlocked ? 1 : 0;
				}
				Lines.Add(BlockedCount > 0
					? TPair<FString, FLinearColor>(FString::Printf(TEXT("%d of %d copies blocked"), BlockedCount, Box.Copies), Blocked)
					: TPair<FString, FLinearColor>(FString::Printf(TEXT("%d %s fit"), Box.Copies, Box.Copies == 1 ? TEXT("copy") : TEXT("copies")), TextDim));
			}
			return Lines;
		};

		for (int32 i = 0; i < Measured.Boxes.Num(); i++)
		{
			const FMeasureBox& Box = Measured.Boxes[i];
			for (int32 Copy = 1; Copy <= Box.Copies; Copy++)
			{
				const bool bBlocked = Box.Blocked.IsValidIndex(Copy - 1) && Box.Blocked[Copy - 1];
				Outline(Box, Copy, bBlocked ? Blocked : Ghost, 1.5f);
			}
			const bool bActive = i == Measured.Active && Measured.bMode;
			const FVector2f Top = Outline(Box, 0, bActive ? Accent : Accent.CopyWithNewOpacity(0.7f), bActive ? 2.5f : 2.f);
			PaintTag(AllottedGeometry, OutDrawElements, TagLayer, Top, BoxLines(Box, true), Accent);
		}

		const auto PaintLine = [&](const FMeasureLine& Line, const FLinearColor& Color)
		{
			const FVector2f A = ToLocal(Line.A);
			const FVector2f B = ToLocal(Line.B);
			TArray<FVector2f> Points = { A, B };
			FSlateDrawElement::MakeLines(OutDrawElements, LineLayer, Paint, Points, ESlateDrawEffect::None, Color, true, 2.f);
			const float Length = FVector2f::Distance(Line.A, Line.B);
			TArray<TPair<FString, FLinearColor>> Lines;
			Lines.Add({ FString::Printf(TEXT("%s m"), *FormatMetres(Length)), TextMain });
			Lines.Add({ FString::Printf(TEXT("%s foundations"), *FormatMetres(Length / 8.f)), TextDim });
			PaintTag(AllottedGeometry, OutDrawElements, TagLayer, (A + B) * 0.5f, Lines, Color);
		};
		for (const FMeasureLine& Line : Measured.Lines)
		{
			PaintLine(Line, Accent.CopyWithNewOpacity(0.85f));
		}

		if (bDraft)
		{
			if (Measured.bLineDrag)
			{
				PaintLine(Measured.DraftLine, FLinearColor::White);
			}
			else if (Measured.Draft.Size().X > 0.f || Measured.Draft.Size().Y > 0.f)
			{
				const FVector2f Top = Outline(Measured.Draft, 0, FLinearColor::White, 2.f);
				PaintTag(AllottedGeometry, OutDrawElements, TagLayer, Top, BoxLines(Measured.Draft, false),
					Measured.Draft.bFoundationGrid ? Accent : FLinearColor(0.6f, 0.6f, 0.62f, 1.f));
			}
		}
		return TagLayer + 1;
	}

	/** A left click on the map that did not drag: select the building under the cursor, or clear the selection. */
	void OnMapClick(const FVector2f& ScreenPosition)
	{
		UAtlasSubsystem* Atlas = Sub();
		LogWindowClick(TEXT("click"), ScreenPosition);
		if (!Atlas || Atlas->IsPointerOverPanel(View) || !Atlas->IsPointOnMap(ScreenPosition, View))
		{
			return;
		}
		// In wire mode a click connects power instead of opening the building's panel.
		if (Atlas->IsWiring(View))
		{
			Atlas->ClickWire(View);
			return;
		}
		const bool bSelected = Atlas->SelectHovered();
		if (View == EView::Window)
		{
			UE_LOG(LogFactoryAtlas, Verbose, TEXT("Window HUD: select %s"), bSelected ? TEXT("worked") : TEXT("found nothing"));
		}
		if (!bSelected)
		{
			Atlas->ClearSelection();
		}
		ClockDraft.Reset();
		bRecipePicker = false;
	}

	/**
	 * Ctrl+right click (bCopy) or Ctrl+left click on the map: copies from, or pastes onto, the building under the
	 * cursor without opening its panel; a toast by the cursor says what happened. False when there is no
	 * building there to use, so the click goes on to the map as usual.
	 */
	/** Diagnostics for the map window: what the HUD's click handling saw. Silent for the game's own map. */
	void LogWindowClick(const TCHAR* What, const FVector2f& ScreenPosition) const
	{
		const UAtlasSubsystem* Atlas = Sub();
		if (View != EView::Window || !Atlas)
		{
			return;
		}
		UE_LOG(LogFactoryAtlas, Verbose, TEXT("Window HUD %s at %.0f,%.0f: on map %d, over panel %d, hover %d (view %d)"), What,
			ScreenPosition.X, ScreenPosition.Y, Atlas->IsPointOnMap(ScreenPosition, View) ? 1 : 0, Atlas->IsPointerOverPanel(View) ? 1 : 0,
			Atlas->GetHover().bValid ? 1 : 0, int32(Atlas->GetHoverView()));
	}

	bool OnMapClipboardClick(const FVector2f& ScreenPosition, bool bCopy)
	{
		UAtlasSubsystem* Atlas = Sub();
		LogWindowClick(bCopy ? TEXT("copy") : TEXT("paste"), ScreenPosition);
		if (!Atlas || Atlas->IsPointerOverPanel(View) || !Atlas->IsPointOnMap(ScreenPosition, View))
		{
			return false;
		}
		if (bCopy)
		{
			if (!Atlas->CopyHovered())
			{
				return false;
			}
			PlayResultSound(*Atlas, EClipboardSound::Copy);
			return true;
		}
		DragPasted.Reset();
		if (!Atlas->PasteHovered(&DragPasted))
		{
			return false;
		}
		PlayResultSound(*Atlas, EClipboardSound::Paste);
		// Starts a drag-paste: holding the button and moving on pastes onto each building passed over.
		LastDragSource = Atlas->GetHover().Source;
		Atlas->SetHoverEveryFrame(true);
		return true;
	}

	/** While a Ctrl+left press that pasted is held: paste onto each further building under the cursor, once. */
	void OnMapDragPaste(const FVector2f& ScreenPosition)
	{
		UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || Atlas->IsPointerOverPanel(View) || !Atlas->IsPointOnMap(ScreenPosition, View))
		{
			return;
		}
		// Selecting rebuilds the panel's lists, so only a change of hovered building is worth looking at.
		const FHover& Hover = Atlas->GetHover();
		if (!Hover.bValid || Hover.bMarker || Hover.Source == LastDragSource)
		{
			return;
		}
		LastDragSource = Hover.Source;
		// A belt or a long wall is several shapes on the map but one building; PasteHovered skips what Done holds.
		FText Text;
		bool bError = false;
		float Alpha = 0.f;
		// Buildings that can't take the paste stay quiet here: a drag crosses plenty of them.
		if (Atlas->PasteHovered(&DragPasted) && Atlas->GetToast(Text, bError, Alpha) && !bError)
		{
			Atlas->ShowToast(FText::FromString(FString::Printf(TEXT("Pasted onto %d buildings."), DragPasted.Num())));
			Atlas->PlayClipboardSound(EClipboardSound::Paste);
		}
	}

	/** After a Ctrl-click acted: its own sound, or the error sound when the toast it left says it failed. */
	static void PlayResultSound(UAtlasSubsystem& Atlas, EClipboardSound Success)
	{
		FText Text;
		bool bError = false;
		float Alpha = 0.f;
		Atlas.PlayClipboardSound(Atlas.GetToast(Text, bError, Alpha) && bError ? EClipboardSound::Error : Success);
	}

	/** A plain press on the map: true when it landed on a port, which starts a belt drag. */
	bool OnMapPortPress(const FVector2f& ScreenPosition)
	{
		UAtlasSubsystem* Atlas = Sub();
		return Atlas && !Atlas->IsWiring(View) && Atlas->PressBeltPort(View, ScreenPosition);
	}

	/** A right press over this map in wire mode: true when it let go of a running wire. */
	bool OnMapWireDrop(const FVector2f& ScreenPosition)
	{
		UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || !Atlas->IsWiring(View) || Atlas->IsPointerOverPanel(View) || !Atlas->IsPointOnMap(ScreenPosition, View))
		{
			return false;
		}
		return Atlas->DropWire();
	}

	void EndDragPaste()
	{
		DragPasted.Reset();
		LastDragSource = FVector4f::Zero();
		if (UAtlasSubsystem* Atlas = Sub())
		{
			Atlas->SetHoverEveryFrame(false);
		}
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

		UAtlasSubsystem* Atlas = Sub();
		if (!Atlas)
		{
			return;
		}

		if (UAtlasMapHud* Host = Owner.Get())
		{
			Host->UpdateFullscreen(Atlas->IsFullscreen());
		}

		// Measured from the panels' shapes, because a pressed button changes which widgets count as hovered.
		const FVector2D Cursor = FSlateApplication::Get().GetCursorPos();
		const bool bOverPanel = (Panel.IsValid() && Panel->GetTickSpaceGeometry().IsUnderLocation(Cursor))
			|| (SelectionPanel.IsValid() && Atlas->HasSelection() && SelectionPanel->GetTickSpaceGeometry().IsUnderLocation(Cursor))
			|| (MeasurePanel.IsValid() && HasMeasurePanel() && MeasurePanel->GetTickSpaceGeometry().IsUnderLocation(Cursor))
			|| (MeasureTile.IsValid() && MeasureTile->GetTickSpaceGeometry().IsUnderLocation(Cursor))
			|| (WireTile.IsValid() && Atlas->CanBuildBelts() && WireTile->GetTickSpaceGeometry().IsUnderLocation(Cursor));
		Atlas->SetPointerOverPanel(bOverPanel, View);

		// The map can reach past the bottom of the screen (the game's window frame keeps some of its height),
		// so the hint bar is lifted by however much of this widget is below the viewport.
		HintInset = 0.f;
		if (View == EView::Game && GEngine && GEngine->GameViewport)
		{
			if (const TSharedPtr<SViewport> Viewport = GEngine->GameViewport->GetGameViewportWidget())
			{
				const FGeometry& ViewportGeometry = Viewport->GetTickSpaceGeometry();
				const float ViewportBottom = float(ViewportGeometry.GetAbsolutePosition().Y + ViewportGeometry.GetAbsoluteSize().Y);
				const float Bottom = float(AllottedGeometry.GetAbsolutePosition().Y + AllottedGeometry.GetAbsoluteSize().Y);
				HintInset = FMath::Max(0.f, (Bottom - ViewportBottom) / FMath::Max(AllottedGeometry.Scale, 0.01f));
			}
		}

		// Read once a frame; every line of the configure panel shows a piece of it.
		const bool bWasValid = State.bValid;
		State = Atlas->GetSelectionState();
		Custom = Atlas->GetCustomizeState();
		// Slate measures widgets before this Tick, which runs during paint. Rows built here, or shown because the
		// state read here changed, would be drawn this frame at no size and pop in the next: the flash on a click.
		// Measuring again after such a change lets this frame draw them right.
		bool bRemeasure = State.bValid != bWasValid;
		if (BuiltSelectionVersion != Atlas->GetSelectionVersion())
		{
			BuiltSelectionVersion = Atlas->GetSelectionVersion();
			RebuildCustomizeLists(*Atlas);
			bRemeasure = true;
		}
		SyncDraft(*Atlas);
		bCanPaste = State.bValid && Atlas->CanPasteToSelected();
		Controls = Atlas->GetControls();
		if (Controls.LightColors.Num() != BuiltLightSlots && Controls.bHasLight)
		{
			BuiltLightSlots = Controls.LightColors.Num();
			RebuildLightColors();
			bRemeasure = true;
		}
		if (Atlas->GetMenuVersion() != BuiltMenuVersion || bListDirty)
		{
			BuiltMenuVersion = Atlas->GetMenuVersion();
			bListDirty = false;
			RebuildList(*Atlas);
			bRemeasure = true;
		}
		if (bRemeasure)
		{
			SlatePrepass(GetPrepassLayoutScaleMultiplier());
		}
		// After measuring, since where the panel goes depends on its size.
		if (State.bValid)
		{
			PlaceSelectionPanel(AllottedGeometry, *Atlas);
		}
		Atlas->UpdateMeasureStats(View);
		PlaceMeasurePanel(AllottedGeometry, *Atlas);
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override
	{
		// Under the panels, which are children painted by SCompoundWidget, so they stay readable over a box.
		const int32 MeasureLayer = PaintMeasure(AllottedGeometry, OutDrawElements, LayerId);
		const int32 Layer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, MeasureLayer, InWidgetStyle, bParentEnabled);
		const int32 LabelLayer = PaintHoverLabel(AllottedGeometry, OutDrawElements, Layer);
		const int32 WireLayer = PaintWireLabel(AllottedGeometry, OutDrawElements, LabelLayer);
		const int32 DragLayer = PaintDragLabel(AllottedGeometry, OutDrawElements, WireLayer);
		return PaintToast(AllottedGeometry, OutDrawElements, DragLayer);
	}

	/** In wire mode, by the cursor: what is under it, and once a wire is running, its length, cost, or why it can't be built. */
	int32 PaintWireLabel(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer) const
	{
		const UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || !Atlas->IsWiring(View) || !GetTickSpaceGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos()))
		{
			return Layer;
		}
		const FWireTool& Wire = Atlas->GetWire();
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const FLinearColor Bad(FColor(235, 90, 80));
		const bool bHovered = Atlas->GetHover().bValid && Atlas->GetHoverView() == View && !Atlas->GetHoverName().IsEmpty();
		const FString WireName = Wire.WireName.IsEmpty() ? FString(TEXT("Power Line")) : Wire.WireName.ToString();

		FString Title;
		TArray<TPair<FString, FLinearColor>> Lines;
		if (!Wire.bHasStart)
		{
			Title = Wire.bHasTarget && bHovered ? Atlas->GetHoverName().ToString() : WireName;
			Lines.Add({ FString(Wire.bHasTarget ? TEXT("Click to start a wire here") : TEXT("Click a building with a power connection")), TextDim });
		}
		else
		{
			const bool bOnStart = !Wire.bHasTarget && !Wire.bHasGround;
			Title = Wire.bHasTarget && bHovered ? Atlas->GetHoverName().ToString() : WireName;
			if (bOnStart)
			{
				Lines.Add({ FString(TEXT("The wire starts here. Click another building.")), TextDim });
			}
			else if (Wire.bReady && !Wire.bValid && !Wire.Reason.IsEmpty())
			{
				Lines.Add({ Wire.Reason.ToString(), Bad });
			}
			else if (Wire.bReady && Wire.bValid)
			{
				const FString Cost = Wire.CostText.IsEmpty() ? FString() : FString::Printf(TEXT("    %s"), *Wire.CostText);
				Lines.Add({ FString::Printf(TEXT("%d m%s"), FMath::RoundToInt32(Wire.LengthMetres), *Cost), TextMain });
				if (!Wire.bHasTarget)
				{
					Lines.Add({ FString(TEXT("Places a power pole here")), TextDim });
				}
				if (Wire.bClipping)
				{
					Lines.Add({ FString(TEXT("Clips through other buildings")), TextDim });
				}
			}
			Lines.Add({ FString(TEXT("RMB  drop wire     Esc  stop")), TextDim });
		}

		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2f Padding(9.f, 4.f);
		const FVector2f TitleSize(Measure->Measure(Title, Style.FontBold));
		const float TitleBand = TitleSize.Y + Padding.Y * 2.f;
		float Width = TitleSize.X;
		float LineHeight = 0.f;
		for (const TPair<FString, FLinearColor>& Line : Lines)
		{
			const FVector2f Size(Measure->Measure(Line.Key, Style.Font));
			Width = FMath::Max(Width, Size.X);
			LineHeight = Size.Y;
		}
		const FVector2f BoxSize(Width + Padding.X * 2.f, TitleBand + LineHeight * Lines.Num() + Padding.Y * 2.f);

		const FVector2f LocalSize(AllottedGeometry.GetLocalSize());
		FVector2f Position = FVector2f(GetTickSpaceGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())) + FVector2f(18.f, 18.f);
		Position.X = FMath::Min(Position.X, LocalSize.X - BoxSize.X - 4.f);
		Position.Y = FMath::Min(Position.Y, LocalSize.Y - BoxSize.Y - 4.f);

		const FLinearColor Band = Wire.bHasStart && Wire.bReady && !Wire.bValid && !Wire.Reason.IsEmpty() ? Bad : Accent;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, BodyColor);
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 2, AllottedGeometry.ToPaintGeometry(FVector2f(BoxSize.X, TitleBand), FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, Band);
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToPaintGeometry(TitleSize, FSlateLayoutTransform(Position + Padding)),
			Title, Style.FontBold, ESlateDrawEffect::None, FLinearColor::White);
		FVector2f LinePosition = Position + FVector2f(Padding.X, TitleBand + Padding.Y);
		for (const TPair<FString, FLinearColor>& Line : Lines)
		{
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToPaintGeometry(FVector2f(Width, LineHeight), FSlateLayoutTransform(LinePosition)),
				Line.Key, Style.Font, ESlateDrawEffect::None, Line.Value);
			LinePosition.Y += LineHeight;
		}
		return Layer + 3;
	}

	/** While a belt is dragged from a port: which belt, which build mode, the keys, and why it can't be built. */
	int32 PaintDragLabel(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer) const
	{
		const UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || !Atlas->IsDraggingBelt() || Atlas->GetBeltDrag().View != View)
		{
			return Layer;
		}
		const FBeltDrag& Drag = Atlas->GetBeltDrag();
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const FLinearColor Bad(FColor(235, 90, 80));

		static const TCHAR* const Fallback[] = { TEXT("Conveyor Belt"), TEXT("Pipeline"), TEXT("Hypertube"), TEXT("Railway") };
		const FString Title = Drag.BeltName.IsEmpty() ? FString(Fallback[FMath::Min(int32(Drag.Start.Kind), 3)]) : Drag.BeltName.ToString();
		TArray<TPair<FString, FLinearColor>> Lines;
		if (!Drag.ModeName.IsEmpty())
		{
			Lines.Add({ FString::Printf(TEXT("Mode  %s"), *Drag.ModeName.ToString()), TextMain });
		}
		if (Drag.bReady && !Drag.bValid && !Drag.Reason.IsEmpty())
		{
			Lines.Add({ Drag.Reason.ToString(), Bad });
		}
		else if (Drag.bReady && Drag.bValid && Drag.bClipping)
		{
			Lines.Add({ FString(TEXT("Clips through other buildings")), TextDim });
		}
		else if (!Drag.bHasTarget)
		{
			const TCHAR* Hint = Drag.bReady && Drag.bValid ? TEXT("Release to end it here, or on a port")
				: Drag.Start.bAnyFlow ? TEXT("Release on a bright port or the ground")
				: Drag.Start.bOutput ? TEXT("Release on an input or the ground") : TEXT("Release on an output or the ground");
			Lines.Add({ FString(Hint), TextDim });
		}
		Lines.Add({ FString(TEXT("R  mode     T  tier     Esc  cancel")), TextDim });

		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		const FVector2f Padding(9.f, 4.f);
		const FVector2f TitleSize(Measure->Measure(Title, Style.FontBold));
		const float TitleBand = TitleSize.Y + Padding.Y * 2.f;
		float Width = TitleSize.X;
		float LineHeight = 0.f;
		for (const TPair<FString, FLinearColor>& Line : Lines)
		{
			const FVector2f Size(Measure->Measure(Line.Key, Style.Font));
			Width = FMath::Max(Width, Size.X);
			LineHeight = Size.Y;
		}
		const FVector2f BoxSize(Width + Padding.X * 2.f, TitleBand + LineHeight * Lines.Num() + Padding.Y * 2.f);

		const FVector2f LocalSize(AllottedGeometry.GetLocalSize());
		FVector2f Position = FVector2f(GetTickSpaceGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())) + FVector2f(18.f, 18.f);
		Position.X = FMath::Min(Position.X, LocalSize.X - BoxSize.X - 4.f);
		Position.Y = FMath::Min(Position.Y, LocalSize.Y - BoxSize.Y - 4.f);

		const FLinearColor Band = Drag.bReady && !Drag.bValid ? Bad : Accent;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, BodyColor);
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 2, AllottedGeometry.ToPaintGeometry(FVector2f(BoxSize.X, TitleBand), FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, Band);
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToPaintGeometry(TitleSize, FSlateLayoutTransform(Position + Padding)),
			Title, Style.FontBold, ESlateDrawEffect::None, FLinearColor::White);
		FVector2f LinePosition = Position + FVector2f(Padding.X, TitleBand + Padding.Y);
		for (const TPair<FString, FLinearColor>& Line : Lines)
		{
			FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToPaintGeometry(FVector2f(Width, LineHeight), FSlateLayoutTransform(LinePosition)),
				Line.Key, Style.Font, ESlateDrawEffect::None, Line.Value);
			LinePosition.Y += LineHeight;
		}
		return Layer + 3;
	}

	/** What a Ctrl-click copy or paste did, just above the cursor, fading out after a moment. */
	int32 PaintToast(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer) const
	{
		const UAtlasSubsystem* Atlas = Sub();
		FText Text;
		bool bError = false;
		float Alpha = 0.f;
		if (!Atlas || !Atlas->GetToast(Text, bError, Alpha))
		{
			return Layer;
		}
		// Both the game's map and the map window have a HUD; the toast belongs by the cursor, in whichever it is over.
		if (!GetTickSpaceGeometry().IsUnderLocation(FSlateApplication::Get().GetCursorPos()))
		{
			return Layer;
		}
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const FString String = Text.ToString();
		const FVector2f Padding(8.f, 4.f);
		const FVector2f TextSize(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(String, Style.Font));
		const FVector2f BoxSize = TextSize + Padding * 2.f;
		const FVector2f LocalSize(AllottedGeometry.GetLocalSize());
		FVector2f Position = FVector2f(GetTickSpaceGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())) + FVector2f(16.f, -BoxSize.Y - 10.f);
		Position.X = FMath::Clamp(Position.X, 4.f, FMath::Max(4.f, LocalSize.X - BoxSize.X - 4.f));
		Position.Y = FMath::Clamp(Position.Y, 4.f, FMath::Max(4.f, LocalSize.Y - BoxSize.Y - 4.f));

		const FLinearColor Edge = bError ? FLinearColor(FColor(235, 90, 80)) : Accent;
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 1, AllottedGeometry.ToPaintGeometry(BoxSize, FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, BodyColor.CopyWithNewOpacity(BodyColor.A * Alpha));
		FSlateDrawElement::MakeBox(OutDrawElements, Layer + 2, AllottedGeometry.ToPaintGeometry(FVector2f(3.f, BoxSize.Y), FSlateLayoutTransform(Position)),
			&Style.White, ESlateDrawEffect::None, Edge.CopyWithNewOpacity(Alpha));
		FSlateDrawElement::MakeText(OutDrawElements, Layer + 3, AllottedGeometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(Position + Padding)),
			String, Style.Font, ESlateDrawEffect::None, TextMain.CopyWithNewOpacity(Alpha));
		return Layer + 3;
	}

	int32 PaintHoverLabel(const FGeometry& AllottedGeometry, FSlateWindowElementList& OutDrawElements, int32 Layer) const
	{
		const UAtlasSubsystem* Atlas = Sub();
		if (!Atlas || !Atlas->GetSettings().bHoverLabel || !Atlas->GetHover().bValid || Atlas->GetHoverView() != View || Atlas->GetHoverName().IsEmpty()
			|| Atlas->IsDraggingBelt() || Atlas->GetMeasure(View).bPressed || Atlas->IsWiring(View))
		{
			return Layer;
		}

		const FAtlasStyle& Style = FAtlasStyle::Get();
		const FHover& Hover = Atlas->GetHover();
		const FSettings& Settings = Atlas->GetSettings();

		const FString Title = Atlas->GetHoverName().ToString();
		TArray<FString> Lines;
		const EStatusLight Status = Atlas->GetHoverStatus();
		if (Status != EStatusLight::None)
		{
			Lines.Add(FString::Printf(TEXT("Status  %s"), StatusLightName(Status)));
		}
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
		FVector2f Position = FVector2f(GetTickSpaceGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos())) + FVector2f(16.f, 16.f);
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
	/** Centres the configure panel just above the selected building, or below it when there is no room, kept on screen. */
	void PlaceSelectionPanel(const FGeometry& Geometry, const UAtlasSubsystem& Atlas)
	{
		if (!SelectionPanel.IsValid())
		{
			return;
		}
		constexpr float Gap = 10.f;
		constexpr float Edge = 4.f;

		// The anchor is in window pixels; the panel is laid out in this widget's units.
		const FSlateRenderTransform ToLocal = Geometry.GetAccumulatedRenderTransform().Inverse();
		const FVector2f Anchor = ToLocal.TransformPoint(Atlas.GetSelectionAnchor(View));
		const float Reach = ToLocal.TransformVector(FVector2f(Atlas.GetSelectionAnchorRadius(View), 0.f)).Size();
		// Measured now, not last frame: a freshly opened or rebuilt panel would otherwise be placed by its old
		// size for one frame, then jump, which shows as a flash on every click.
		SelectionPanel->SlatePrepass(GetPrepassLayoutScaleMultiplier());
		const FVector2f PanelSize(SelectionPanel->GetDesiredSize());
		const FVector2f Bounds(Geometry.GetLocalSize());

		float X = Anchor.X - PanelSize.X * 0.5f;
		float Y = Anchor.Y - Reach - Gap - PanelSize.Y;
		if (Y < Edge)
		{
			Y = Anchor.Y + Reach + Gap;
		}
		X = FMath::Max(FMath::Min(X, Bounds.X - PanelSize.X - Edge), Edge);
		Y = FMath::Max(FMath::Min(Y, Bounds.Y - PanelSize.Y - Edge), Edge);
		SelectionOffset = FVector2f(FMath::RoundToFloat(X), FMath::RoundToFloat(Y));
	}

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
					.Visibility(View == EView::Game ? EVisibility::Visible : EVisibility::Collapsed) // The window is all map already
					.OnClicked_Lambda([]() { if (Sub()) { Sub()->SetFullscreen(!Sub()->IsFullscreen()); } return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Style.Font).ColorAndOpacity(FSlateColor(FLinearColor::White))
						.Text_Lambda([]() { return FText::FromString(Sub() && Sub()->IsFullscreen() ? TEXT("Windowed") : TEXT("Fullscreen")); })
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SAtlasButton)
					.ButtonStyle(&Style.Flat)
					.IsFocusable(false)
					.ContentPadding(FMargin(6.f, 0.f))
					.Visibility(View == EView::Game ? EVisibility::Visible : EVisibility::Collapsed)
					.OnClicked_Lambda([]() { MapWindow::Toggle(); return FReply::Handled(); })
					[
						SNew(STextBlock).Font(Style.Font).Text(FText::FromString(TEXT("Second window")))
						.ColorAndOpacity_Lambda([]() { return FSlateColor(MapWindow::IsOpen() ? Accent : FLinearColor::White); })
					]
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
			+ SVerticalBox::Slot().AutoHeight()
			[
				// All of it goes when the player has asked for no spoilers.
				SNew(SVerticalBox)
				.Visibility_Lambda([]() { return Sub() && Sub()->AreSpoilersHidden() ? EVisibility::Collapsed : EVisibility::Visible; })
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
			]
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
		return MakeToggleRow(Label,
			[Member]() { return Sub() && Sub()->GetSettings().*Member; },
			[Member]()
			{
				if (UAtlasSubsystem* Atlas = Sub())
				{
					Atlas->EditSettings().*Member = !(Atlas->GetSettings().*Member);
					Atlas->SaveSettings();
				}
			});
	}

	/** A toggle with a label, for settings that need more than flipping a bool when changed. */
	TSharedRef<SWidget> MakeToggleRow(const TCHAR* Label, TFunction<bool()> IsOn, TFunction<void()> OnClick)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 3.f, 8.f, 3.f))
			[
				MakeToggle(MoveTemp(IsOn), MoveTemp(OnClick))
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
								HeaderButton(TEXT("Copy"), TEXT("Copy this building's settings, color, pattern and material. On the map: Ctrl+right click a building."),
									[]() { return Sub() && Sub()->CanCopySelected(); },
									[]() { if (Sub()) { Sub()->CopySelected(); } })
							]
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
							[
								HeaderButton(TEXT("Paste"), TEXT("Paste whatever of the copied settings and look this building can take. On the map: Ctrl+left click a building, or hold and drag across several."),
									[this]() { return bCanPaste; },
									[this]() { if (Sub()) { Sub()->PasteToSelected(); } ClockDraft.Reset(); bRecipePicker = false; })
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
					// What the last copy or paste did.
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 6.f, 10.f, 0.f))
					[
						SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)
						.Text_Lambda([]() { return Sub() ? Sub()->GetClipboardStatus() : FText::GetEmpty(); })
						.Visibility_Lambda([]() { return Sub() && !Sub()->GetClipboardStatus().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
					]
					// Machine controls. A collapsed slot drops its padding, so other buildings get no gap here.
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 8.f, 10.f, 10.f))
					[
						SNew(SVerticalBox)
						.Visibility_Lambda([this]() { return State.bValid && State.bFactory ? EVisibility::Visible : EVisibility::Collapsed; })
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
						+ SVerticalBox::Slot().AutoHeight()
						[
							// Only for buildings that take a Somersloop at all.
							SNew(SBox)
							.Padding(FMargin(0.f, 3.f, 0.f, 0.f))
							.Visibility_Lambda([this]() { return State.bCanBoost ? EVisibility::Visible : EVisibility::Collapsed; })
							[
								Line([this]()
								{
									return State.MaxBoost > 1.001f
										? FString::Printf(TEXT("Somersloop  installed, output x%.1f"), State.MaxBoost)
										: FString(TEXT("Somersloop  none"));
								})
							]
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
						+ SVerticalBox::Slot().AutoHeight()
						[
							// Only when something stops the controls above from working.
							SNew(SBox)
							.Padding(FMargin(0.f, 6.f, 0.f, 0.f))
							.Visibility_Lambda([this]()
							{
								return State.bValid && (!State.bAuthority || !State.bCanChangeClock) ? EVisibility::Visible : EVisibility::Collapsed;
							})
							[
								SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)
								.Text_Lambda([this]()
								{
									return FText::FromString(!State.bAuthority
										? TEXT("Only the host can change buildings.")
										: TEXT("This building has no clock speed."));
								})
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 8.f, 10.f, 2.f))
					[
						MakeControlsSection()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(10.f, 8.f, 10.f, 10.f))
					[
						MakeCustomizeSection()
					]
				]
			];
	}

	/** A label column for the settings rows, so their controls line up. */
	TSharedRef<SWidget> RowLabel(const TCHAR* Text)
	{
		return SNew(SBox).WidthOverride(64.f).VAlign(VAlign_Center)
			[
				MakeLabel(Text, FAtlasStyle::Get().Font, TextDim)
			];
	}

	/** Names, load modes, switches, flow limits and lights. Each row shows only when the building has that setting. */
	TSharedRef<SWidget> MakeControlsSection()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const auto Shown = [this](bool FControls::* Flag)
		{
			return [this, Flag]() { return Controls.*Flag ? EVisibility::Visible : EVisibility::Collapsed; };
		};
		const auto Line = [&Style](TFunction<FString()> Text)
		{
			return SNew(STextBlock).Font(Style.Font).ColorAndOpacity(FSlateColor(TextMain))
				.Text_Lambda([Text]() { return FText::FromString(Text()); });
		};
		const auto ToggleRow = [&Style](const TCHAR* Label, TFunction<bool()> IsOn, TFunction<void()> OnClick)
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(2.f, 3.f, 8.f, 3.f))[MakeToggle(IsOn, OnClick)]
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[MakeLabel(Label, Style.Font, TextMain)];
		};
		const auto ApplyLight = [this](TOptional<bool> On, TOptional<bool> Night, TOptional<float> Intensity, TOptional<int32> Slot)
		{
			if (Sub())
			{
				Sub()->SetSelectedLight(On.Get(Controls.bLightOn), Night.Get(Controls.bNightOnly), Intensity.Get(Controls.Intensity), Slot.Get(Controls.LightColorSlot));
			}
		};

		return SNew(SVerticalBox)
			.Visibility_Lambda([this]() { return State.bValid && Controls.Any() ? EVisibility::Visible : EVisibility::Collapsed; })
			.IsEnabled_Lambda([this]() { return State.bAuthority; })
			// Divides it from the machine controls above, when there are any.
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
			[
				SNew(SBox).HeightOverride(1.f)
				.Visibility_Lambda([this]() { return State.bFactory ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SImage).Image(&Style.White).ColorAndOpacity(FSlateColor(RowColor))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda(Shown(&FControls::bHasName))
				+ SHorizontalBox::Slot().AutoWidth()[RowLabel(TEXT("Name"))]
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(SBorder)
					.BorderImage(&Style.White)
					.BorderBackgroundColor(FSlateColor(RowColor))
					.Padding(FMargin(5.f, 3.f))
					[
						SNew(SEditableText)
						.Style(&Style.TextInput)
						.SelectAllTextWhenFocused(true)
						.ClearKeyboardFocusOnCommit(true)
						.ToolTip(MakeTip(FText::FromString(TEXT("Press Enter to rename."))))
						.Text_Lambda([this]() { return Controls.Name; })
						.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
						{
							if (Commit != ETextCommit::OnCleared && !Text.EqualTo(Controls.Name) && Sub())
							{
								Sub()->SetSelectedName(Text);
							}
						})
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(SHorizontalBox)
				.Visibility_Lambda(Shown(&FControls::bHasLoadMode))
				+ SHorizontalBox::Slot().AutoWidth()[RowLabel(TEXT("Mode"))]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(0.f, 0.f, 3.f, 0.f))
				[
					MakeTextButton(FText::FromString(TEXT("Load")), [this]() { return Controls.bLoadMode; },
						[]() { if (Sub()) { Sub()->SetSelectedLoadMode(true); } })
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(FMargin(3.f, 0.f, 0.f, 0.f))
				[
					MakeTextButton(FText::FromString(TEXT("Unload")), [this]() { return !Controls.bLoadMode; },
						[]() { if (Sub()) { Sub()->SetSelectedLoadMode(false); } })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(SBox)
				.Visibility_Lambda(Shown(&FControls::bHasSwitch))
				[
					ToggleRow(TEXT("Switched on"), [this]() { return Controls.bSwitchOn; },
						[this]() { if (Sub()) { Sub()->SetSelectedSwitch(!Controls.bSwitchOn); } })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(SVerticalBox)
				.Visibility_Lambda(Shown(&FControls::bHasFlowLimit))
				+ SVerticalBox::Slot().AutoHeight()
				[
					Line([this]()
					{
						// The game keeps m3 per second; players read m3 per minute.
						const float Ratio = FlowDraft.IsSet() ? FlowDraft.GetValue() : FlowRatio();
						if (Ratio >= 1.f)
						{
							return FString::Printf(TEXT("Flow limit  none (max %.0f m3/min)"), Controls.MaxFlow * 60.f);
						}
						return FString::Printf(TEXT("Flow limit  %.1f m3/min"), Ratio * Controls.MaxFlow * 60.f);
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
				[
					SNew(SAtlasSlider)
					.Style(&Style.Slider)
					.IsFocusable(false)
					.Value_Lambda([this]() { return FlowDraft.IsSet() ? FlowDraft.GetValue() : FlowRatio(); })
					.OnValueChanged_Lambda([this](float Value)
					{
						// Whole tenths of a cubic metre per minute.
						const float PerMinute = FMath::RoundToFloat(Value * Controls.MaxFlow * 600.f) / 10.f;
						FlowDraft = Controls.MaxFlow > 0.f ? FMath::Clamp(PerMinute / (Controls.MaxFlow * 60.f), 0.f, 1.f) : 1.f;
					})
					.OnMouseCaptureEnd_Lambda([this]()
					{
						if (FlowDraft.IsSet() && Sub())
						{
							const float Ratio = FlowDraft.GetValue();
							Sub()->SetSelectedFlowLimit(Ratio >= 1.f ? -1.f : Ratio * Controls.MaxFlow);
						}
						FlowDraft.Reset();
					})
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 6.f))
			[
				SNew(SVerticalBox)
				.Visibility_Lambda(Shown(&FControls::bHasLight))
				+ SVerticalBox::Slot().AutoHeight()
				[
					ToggleRow(TEXT("Light on"), [this]() { return Controls.bLightOn; },
						[this, ApplyLight]() { ApplyLight(!Controls.bLightOn, {}, {}, {}); })
				]
				+ SVerticalBox::Slot().AutoHeight()
				[
					ToggleRow(TEXT("Only at night"), [this]() { return Controls.bNightOnly; },
						[this, ApplyLight]() { ApplyLight({}, !Controls.bNightOnly, {}, {}); })
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
				[
					Line([this]()
					{
						return FString::Printf(TEXT("Brightness  %d%%"), FMath::RoundToInt32((LightDraft.IsSet() ? LightDraft.GetValue() : Controls.Intensity) * 100.f));
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))
				[
					SNew(SAtlasSlider)
					.Style(&Style.Slider)
					.IsFocusable(false)
					.Value_Lambda([this]() { return LightDraft.IsSet() ? LightDraft.GetValue() : Controls.Intensity; })
					.OnValueChanged_Lambda([this](float Value) { LightDraft = FMath::RoundToFloat(Value * 100.f) / 100.f; })
					.OnMouseCaptureEnd_Lambda([this, ApplyLight]()
					{
						if (LightDraft.IsSet())
						{
							ApplyLight({}, {}, LightDraft.GetValue(), {});
						}
						LightDraft.Reset();
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
				[
					SAssignNew(LightColorList, SWrapBox).UseAllottedSize(true)
				]
			];
	}

	float FlowRatio() const
	{
		return Controls.FlowLimit < 0.f || Controls.MaxFlow <= 0.f ? 1.f : FMath::Clamp(Controls.FlowLimit / Controls.MaxFlow, 0.f, 1.f);
	}

	/** The light color chips follow the game's slot count, which is only known once a light is selected. */
	void RebuildLightColors()
	{
		if (!LightColorList)
		{
			return;
		}
		const FAtlasStyle& Style = FAtlasStyle::Get();
		LightColorList->ClearChildren();
		for (int32 i = 0; i < BuiltLightSlots; i++)
		{
			LightColorList->AddSlot()
			[
				MakeChoice(FText::FromString(FString::Printf(TEXT("Light color %d"), i + 1)), [this, i]() { return Controls.LightColorSlot == i; },
					[this, i]()
					{
						if (Sub())
						{
							Sub()->SetSelectedLight(Controls.bLightOn, Controls.bNightOnly, Controls.Intensity, i);
						}
					},
					SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)
					[
						SNew(SImage).Image(&Style.White)
						.ColorAndOpacity_Lambda([this, i]() { return FSlateColor(Controls.LightColors.IsValidIndex(i) ? Controls.LightColors[i] : FLinearColor::White); })
					])
			];
		}
	}

	/** A flat text button for the selection's title band, dimmed while it can't be used. */
	TSharedRef<SWidget> HeaderButton(const TCHAR* Label, const TCHAR* Tooltip, TFunction<bool()> IsEnabled, TFunction<void()> OnClick)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SAtlasButton)
			.ButtonStyle(&Style.Flat)
			.IsFocusable(false)
			.ToolTip(MakeTip(FText::FromString(Tooltip)))
			.ContentPadding(FMargin(6.f, 0.f))
			.IsEnabled_Lambda([IsEnabled]() { return IsEnabled(); })
			.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
			[
				SNew(STextBlock).Text(FText::FromString(Label)).Font(Style.Font)
				.ColorAndOpacity_Lambda([IsEnabled]() { return FSlateColor(IsEnabled() ? FLinearColor::White : FLinearColor(1.f, 1.f, 1.f, 0.45f)); })
			];
	}

	/** Color, pattern and material. Each part shows only when the building takes that kind of customization. */
	TSharedRef<SWidget> MakeCustomizeSection()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const auto Heading = [&Style](const TCHAR* Text) { return MakeLabel(Text, Style.FontSmall, TextDim); };
		const auto Has = [](TArray<FCustomizeOption> FCustomizeOptions::* List)
		{
			return [List]() { return Sub() && !(Sub()->GetCustomizeOptions().*List).IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; };
		};

		return SNew(SVerticalBox)
			.Visibility_Lambda([this]() { return State.bValid && HasCustomizeOptions() ? EVisibility::Visible : EVisibility::Collapsed; })
			.IsEnabled_Lambda([this]() { return State.bAuthority; })
			// Divides it from the machine controls or settings above, when there are any.
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 8.f))
			[
				SNew(SBox).HeightOverride(1.f)
				.Visibility_Lambda([this]() { return State.bFactory || Controls.Any() ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					SNew(SImage).Image(&Style.White).ColorAndOpacity(FSlateColor(RowColor))
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				MakeAppearanceHeader()
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([]() { return Sub() && Sub()->IsAppearanceOpen() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
				[
					SNew(SVerticalBox)
					.Visibility_Lambda(Has(&FCustomizeOptions::Swatches))
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))[Heading(TEXT("Color"))]
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(SwatchList, SWrapBox).UseAllottedSize(true)]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 0.f))[MakeCustomColorEditor()]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					SNew(SVerticalBox)
					.Visibility_Lambda(Has(&FCustomizeOptions::Patterns))
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)[Heading(TEXT("Pattern"))]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox).WidthOverride(70.f)
							.IsEnabled_Lambda([this]() { return Custom.Pattern != nullptr; })
							[
								MakeTextButton(FText::FromString(TEXT("Rotate")), []() { return false; },
									[]() { if (Sub()) { Sub()->RotateSelectedPattern(); } })
							]
						]
					]
					+ SVerticalBox::Slot().AutoHeight()[SAssignNew(PatternList, SWrapBox).UseAllottedSize(true)]
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				[
					SNew(SVerticalBox)
					.Visibility_Lambda(Has(&FCustomizeOptions::Materials))
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))[Heading(TEXT("Material"))]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SAssignNew(MaterialList, SWrapBox).UseAllottedSize(true)
						.IsEnabled_Lambda([]() { return Sub() && Sub()->GetCustomizeOptions().MaterialNote.IsEmpty(); })
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 3.f, 0.f, 0.f))
					[
						SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim)).AutoWrapText(true)
						.Text_Lambda([]()
						{
							const FText& Note = Sub() ? Sub()->GetCustomizeOptions().MaterialNote : FText::GetEmpty();
							return Note.IsEmpty() ? FText::FromString(TEXT("Costs the new version's parts; the old ones come back, as when you dismantle.")) : Note;
						})
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(FLinearColor(FColor(235, 90, 80)))).AutoWrapText(true)
				.Text_Lambda([]() { return Sub() ? Sub()->GetCustomizeError() : FText::GetEmpty(); })
				.Visibility_Lambda([]() { return Sub() && !Sub()->GetCustomizeError().IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SNew(STextBlock).Font(Style.FontSmall).ColorAndOpacity(FSlateColor(TextDim))
				.Text(FText::FromString(TEXT("Only the host can change buildings.")))
				.Visibility_Lambda([this]() { return State.bAuthority || State.bFactory ? EVisibility::Collapsed : EVisibility::Visible; })
			];
	}

	/** The building's colors right now, for the folded Appearance row. */
	FLinearColor CurrentColor(bool bSecondary) const
	{
		if (Custom.bCustom)
		{
			return bSecondary ? Custom.CustomSecondary : Custom.CustomPrimary;
		}
		if (const UAtlasSubsystem* Atlas = Sub())
		{
			for (const FCustomizeOption& Option : Atlas->GetCustomizeOptions().Swatches)
			{
				if (Option.Desc == Custom.Swatch)
				{
					return bSecondary ? Option.Secondary : Option.Primary;
				}
			}
		}
		return RowColor;
	}

	/**
	 * Color, pattern and material fill much of the panel, so they fold to this one row: the building's current
	 * colors and a button to open them. The choice is kept between buildings and sessions.
	 */
	TSharedRef<SWidget> MakeAppearanceHeader()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const auto Chip = [this, &Style](bool bSecondary)
		{
			return SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)
				[
					SNew(SImage).Image(&Style.White)
					.ColorAndOpacity_Lambda([this, bSecondary]() { return FSlateColor(CurrentColor(bSecondary)); })
				];
		};
		const auto HasSwatches = []() { return Sub() && !Sub()->GetCustomizeOptions().Swatches.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; };

		return SNew(SAtlasButton)
			.ButtonStyle(&Style.Flat)
			.IsFocusable(false)
			.ContentPadding(FMargin(0.f, 2.f))
			.OnClicked_Lambda([]() { if (Sub()) { Sub()->SetAppearanceOpen(!Sub()->IsAppearanceOpen()); } return FReply::Handled(); })
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 8.f, 0.f))
				[
					MakeLabel(TEXT("Appearance"), Style.Font, TextMain)
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 3.f, 0.f))
				[
					SNew(SBox).Visibility_Lambda(HasSwatches)[Chip(false)]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).Visibility_Lambda(HasSwatches)[Chip(true)]
				]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(Style.Font).ColorAndOpacity(FSlateColor(Accent))
					.Text_Lambda([]() { return FText::FromString(Sub() && Sub()->IsAppearanceOpen() ? TEXT("Hide") : TEXT("Show")); })
				]
			];
	}

	/** Shown while the building wears the custom swatch: its two colors, picked by field or hex, and its finish. */
	TSharedRef<SWidget> MakeCustomColorEditor()
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const auto SlotButton = [this, &Style](const TCHAR* Label, bool bSecondary)
		{
			return SNew(SAtlasButton)
				.ButtonStyle(&Style.Tinted)
				.IsFocusable(false)
				.ButtonColorAndOpacity_Lambda([this, bSecondary]() { return FSlateColor(bEditSecondary == bSecondary ? Accent : RowColor); })
				.ContentPadding(FMargin(5.f, 3.f))
				.OnClicked_Lambda([this, bSecondary]() { bEditSecondary = bSecondary; return FReply::Handled(); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 5.f, 0.f))
					[
						SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)
						[
							SNew(SImage).Image(&Style.White)
							.ColorAndOpacity_Lambda([this, bSecondary]() { return FSlateColor(bSecondary ? DraftSecondary : DraftPrimary); })
						]
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						MakeLabel(Label, Style.Font, TextMain)
					]
				];
		};

		return SNew(SVerticalBox)
			.Visibility_Lambda([this]() { return Custom.bCustom ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[SlotButton(TEXT("Primary"), false)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(FMargin(4.f, 0.f, 0.f, 0.f))[SlotButton(TEXT("Secondary"), true)]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[
					SNew(SBorder)
					.BorderImage(&Style.White)
					.BorderBackgroundColor(FSlateColor(RowColor))
					.Padding(FMargin(5.f, 3.f))
					[
						SNew(SBox).WidthOverride(66.f)
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[MakeLabel(TEXT("#"), Style.Font, TextDim)]
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(SEditableText)
								.Style(&Style.TextInput)
								.SelectAllTextWhenFocused(true)
								.ClearKeyboardFocusOnCommit(true)
								.ToolTip(MakeTip(FText::FromString(TEXT("Hex color, like FF8800. Press Enter to apply."))))
								.Text_Lambda([this]() { return FText::FromString(ToHex(EditedDraft())); })
								.OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type Commit)
								{
									FLinearColor Parsed;
									if (Commit != ETextCommit::OnCleared && FromHex(Text.ToString(), Parsed))
									{
										EditedDraft() = Parsed;
										ApplyCustomColor(Custom.Finish);
									}
								})
							]
						]
					]
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 6.f, 0.f, 0.f))
			[
				SAssignNew(ColorField, SAtlasColorField)
				.Color_Lambda([this]() { return EditedDraft(); })
				.OnChanged_Lambda([this](FLinearColor Color) { EditedDraft() = Color; })
				.OnCommitted_Lambda([this]() { ApplyCustomColor(Custom.Finish); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 8.f, 0.f, 0.f))
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([]() { return Sub() && !Sub()->GetCustomizeOptions().Finishes.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))[MakeLabel(TEXT("Finish"), Style.FontSmall, TextDim)]
				+ SVerticalBox::Slot().AutoHeight()[SAssignNew(FinishList, SWrapBox).UseAllottedSize(true)]
			];
	}

	FLinearColor& EditedDraft()
	{
		return bEditSecondary ? DraftSecondary : DraftPrimary;
	}

	FLinearColor EditedDraft() const
	{
		return bEditSecondary ? DraftSecondary : DraftPrimary;
	}

	void ApplyCustomColor(UClass* Finish)
	{
		if (UAtlasSubsystem* Atlas = Sub())
		{
			Atlas->SetSelectedCustomColor(DraftPrimary, DraftSecondary, Finish);
		}
	}

	/**
	 * Outside a drag the draft follows the building, so a refused change (say, not enough items) snaps back.
	 * A building in a preset swatch starts the draft from that swatch's colors, so picking "custom" changes nothing at first.
	 */
	void SyncDraft(const UAtlasSubsystem& Atlas)
	{
		if (ColorField.IsValid() && ColorField->IsDragging())
		{
			return;
		}
		if (Custom.bCustom)
		{
			DraftPrimary = Custom.CustomPrimary;
			DraftSecondary = Custom.CustomSecondary;
			return;
		}
		for (const FCustomizeOption& Option : Atlas.GetCustomizeOptions().Swatches)
		{
			if (Option.Desc == Custom.Swatch)
			{
				DraftPrimary = Option.Primary;
				DraftSecondary = Option.Secondary;
				return;
			}
		}
	}

	bool HasCustomizeOptions() const
	{
		const UAtlasSubsystem* Atlas = Sub();
		if (!Atlas)
		{
			return false;
		}
		const FCustomizeOptions& Options = Atlas->GetCustomizeOptions();
		return !Options.Swatches.IsEmpty() || !Options.Patterns.IsEmpty() || !Options.Materials.IsEmpty();
	}

	/** A square button around Content, outlined in the accent colour while IsCurrent holds. */
	TSharedRef<SWidget> MakeChoice(const FText& Name, TFunction<bool()> IsCurrent, TFunction<void()> OnClick, const TSharedRef<SWidget>& Content)
	{
		const FAtlasStyle& Style = FAtlasStyle::Get();
		return SNew(SBox).Padding(FMargin(0.f, 0.f, 4.f, 4.f))
			[
				SNew(SAtlasButton)
				.ButtonStyle(&Style.Flat)
				.IsFocusable(false)
				.ToolTip(MakeTip(Name))
				.ContentPadding(FMargin(0.f))
				.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
				[
					SNew(SBorder)
					.BorderImage(&Style.White)
					.BorderBackgroundColor_Lambda([IsCurrent]() { return FSlateColor(IsCurrent() ? Accent : RowColor); })
					.Padding(FMargin(2.f))
					[
						Content
					]
				]
			];
	}

	/** Called when the selection changes: the lists depend on what the building accepts. */
	void RebuildCustomizeLists(const UAtlasSubsystem& Atlas)
	{
		if (!SwatchList || !PatternList || !MaterialList || !FinishList)
		{
			return;
		}
		const FAtlasStyle& Style = FAtlasStyle::Get();
		const FCustomizeOptions& Options = Atlas.GetCustomizeOptions();
		SwatchList->ClearChildren();
		PatternList->ClearChildren();
		MaterialList->ClearChildren();
		IconBrushes.Reset();

		const auto IconOf = [this](UTexture2D* Icon) -> const FSlateBrush*
		{
			const TSharedRef<FSlateBrush> Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Icon);
			Brush->ImageSize = FVector2D(30.0, 30.0);
			Brush->DrawAs = ESlateBrushDrawType::Image;
			IconBrushes.Add(Brush);
			return &Brush.Get();
		};

		for (const FCustomizeOption& Option : Options.Swatches)
		{
			UClass* Desc = Option.Desc;
			SwatchList->AddSlot()
			[
				MakeChoice(Option.Name, [this, Desc]() { return Custom.Swatch == Desc; },
					[Desc]() { if (Sub()) { Sub()->SetSelectedSwatch(Desc); } },
					SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)
					[
						// Primary on top, secondary as a strip below, as the game's own swatch picker shows them.
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().FillHeight(3.f)[SNew(SImage).Image(&Style.White).ColorAndOpacity(FSlateColor(Option.Primary))]
						+ SVerticalBox::Slot().FillHeight(1.f)[SNew(SImage).Image(&Style.White).ColorAndOpacity(FSlateColor(Option.Secondary))]
					])
			];
		}
		if (Options.CustomSwatch)
		{
			SwatchList->AddSlot()
			[
				MakeChoice(FText::FromString(TEXT("Custom color")), [this]() { return Custom.bCustom; },
					[this]() { if (!Custom.bCustom) { ApplyCustomColor(nullptr); } },
					SNew(SBox).WidthOverride(22.f).HeightOverride(22.f)[SNew(SAtlasHueChip)])
			];
		}

		FinishList->ClearChildren();
		for (const FCustomizeOption& Option : Options.Finishes)
		{
			UClass* Desc = Option.Desc;
			FinishList->AddSlot()
			[
				SNew(SBox).Padding(FMargin(0.f, 0.f, 4.f, 4.f)).MinDesiredWidth(64.f)
				[
					MakeTextButton(Option.Name, [this, Desc]() { return Custom.Finish == Desc; },
						[this, Desc]() { ApplyCustomColor(Custom.Finish == Desc ? nullptr : Desc); })
				]
			];
		}

		for (int32 i = 0; i < Options.Patterns.Num(); i++)
		{
			const FCustomizeOption& Option = Options.Patterns[i];
			UClass* Desc = Option.Desc;
			// The first entry is "None", which the state reports as no pattern at all.
			const bool bNone = i == 0;
			const TSharedRef<SWidget> Face = Option.Icon
				? StaticCastSharedRef<SWidget>(SNew(SImage).Image(IconOf(Option.Icon)))
				: StaticCastSharedRef<SWidget>(SNew(SBox).WidthOverride(30.f).HeightOverride(30.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						MakeLabel(Option.Name, Style.FontSmall, TextMain)
					]);
			PatternList->AddSlot()
			[
				MakeChoice(Option.Name, [this, Desc, bNone]() { return bNone ? Custom.Pattern == nullptr : Custom.Pattern == Desc; },
					[Desc]() { if (Sub()) { Sub()->SetSelectedPattern(Desc); } },
					SNew(SBox).WidthOverride(30.f).HeightOverride(30.f)[Face])
			];
		}

		for (int32 i = 0; i < Options.Materials.Num(); i++)
		{
			const FCustomizeOption& Option = Options.Materials[i];
			UClass* Desc = Option.Desc;
			const TSharedRef<SWidget> Face = Option.Icon
				? StaticCastSharedRef<SWidget>(SNew(SImage).Image(IconOf(Option.Icon)))
				: StaticCastSharedRef<SWidget>(MakeLabel(Option.Name, Style.FontSmall, TextMain));
			MaterialList->AddSlot()
			[
				MakeChoice(Option.Name, [i]() { return Sub() && Sub()->GetCustomizeOptions().CurrentMaterial == i; },
					[Desc]() { if (Sub()) { Sub()->SetSelectedMaterial(Desc); } },
					SNew(SBox).MinDesiredWidth(30.f).HeightOverride(30.f).HAlign(HAlign_Center).VAlign(VAlign_Center)[Face])
			];
		}
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
			.ToolTip(MakeTip(bSeenOnly
				? FText::FromString(TEXT("Only exist in the game while their part of the world is loaded. Atlas remembers each one it has seen, in every save."))
				: FText::GetEmpty()))
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
			// Hover and display toggles live in the mod settings menu (Mods > Atlas), see UAtlasModConfig.
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))[Heading(TEXT("Map"))]
			+ SVerticalBox::Slot().AutoHeight().Padding(FMargin(0.f, 2.f, 0.f, 2.f))
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
			.ToolTip(MakeTip(FText::FromString(TEXT("Change color"))))
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
	TSharedPtr<SAtlasPanelBorder> MeasurePanel;
	TSharedPtr<SBox> MeasureTile;
	TSharedPtr<SBox> WireTile;
	FVector2f MeasureOffset = FVector2f::ZeroVector;
	/** Local units of this widget below the bottom of the screen; the hint bar sits above them. */
	float HintInset = 0.f;
	FVector2f SelectionOffset = FVector2f::ZeroVector;
	FCustomizeState Custom;
	uint32 BuiltSelectionVersion = MAX_uint32;
	uint32 BuiltMenuVersion = MAX_uint32;
	EView View = EView::Game;
	TSharedPtr<SWrapBox> SwatchList;
	TSharedPtr<SWrapBox> PatternList;
	TSharedPtr<SWrapBox> MaterialList;
	TSharedPtr<SWrapBox> FinishList;
	TSharedPtr<SAtlasColorField> ColorField;
	/** The custom colors being picked, linear. Applied to the building on release or Enter. */
	FLinearColor DraftPrimary = FLinearColor::White;
	FLinearColor DraftSecondary = FLinearColor::White;
	bool bEditSecondary = false;
	bool bCanPaste = false;
	FControls Controls;
	/** Buildings pasted onto during the current Ctrl+left drag, and the map shape last looked at. */
	TSet<const void*> DragPasted;
	FVector4f LastDragSource = FVector4f::Zero();
	TOptional<float> FlowDraft;  // Slider position while dragging, 1 meaning no limit
	TOptional<float> LightDraft;
	TSharedPtr<SWrapBox> LightColorList;
	int32 BuiltLightSlots = 0;
	/** Brushes for the pattern and material icons; the subsystem keeps the textures alive. */
	TArray<TSharedRef<FSlateBrush>> IconBrushes;
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
		if (OwnedProcessor && FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(OwnedProcessor);
		}
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
		// In measure mode a left press on the map measures, unless Space is held, which leaves it to pan the map.
		// Shift measures a line instead of a box. Press, moves and release then belong to the measure tool.
		if (Event.GetEffectingButton() == EKeys::LeftMouseButton && !Event.IsControlDown() && !Event.IsAltDown() && !bSpaceHeld)
		{
			const TSharedPtr<SAtlasHud> Pinned = Hud.Pin();
			if (Pinned && Pinned->OnMapMeasurePress(FVector2f(Event.GetScreenSpacePosition()), Event.IsShiftDown()))
			{
				bMeasurePress = true;
				bDown = false;
				return true;
			}
		}
		// In wire mode a plain right press lets go of the wire being run. Press and release are both swallowed, so
		// the map doesn't open its marker menu.
		if (Event.GetEffectingButton() == EKeys::RightMouseButton && !Event.GetModifierKeys().AnyModifiersDown())
		{
			const TSharedPtr<SAtlasHud> Pinned = Hud.Pin();
			if (Pinned && Pinned->OnMapWireDrop(FVector2f(Event.GetScreenSpacePosition())))
			{
				SwallowUp = EKeys::RightMouseButton;
				bDragPaste = false;
				bDown = false;
				return true;
			}
		}
		// Ctrl+right click copies the building under the cursor, Ctrl+left click pastes onto it. When one lands on
		// a building, its press and release are both swallowed, so the map neither pans nor opens its marker menu.
		const bool bRight = Event.GetEffectingButton() == EKeys::RightMouseButton;
		if (Event.IsControlDown() && (bRight || Event.GetEffectingButton() == EKeys::LeftMouseButton))
		{
			const TSharedPtr<SAtlasHud> Pinned = Hud.Pin();
			if (Pinned && Pinned->OnMapClipboardClick(FVector2f(Event.GetScreenSpacePosition()), bRight))
			{
				SwallowUp = Event.GetEffectingButton();
				bDragPaste = !bRight;
				bDown = false;
				return true;
			}
		}
		// A plain left press on a port starts a belt drag. Press, moves and release then belong to it, so the map
		// neither pans nor opens anything.
		if (Event.GetEffectingButton() == EKeys::LeftMouseButton && !Event.GetModifierKeys().AnyModifiersDown())
		{
			const TSharedPtr<SAtlasHud> Pinned = Hud.Pin();
			if (Pinned && Pinned->OnMapPortPress(FVector2f(Event.GetScreenSpacePosition())))
			{
				bPortPress = true;
				bDown = false;
				return true;
			}
		}
		if (Event.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			DownAt = FVector2f(Event.GetScreenSpacePosition());
			bDown = true;
		}
		return false;
	}

	virtual bool HandleMouseButtonUpEvent(FSlateApplication&, const FPointerEvent& Event) override
	{
		if (bMeasurePress && Event.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bMeasurePress = false;
			if (const TSharedPtr<SAtlasHud> Pinned = Hud.Pin())
			{
				Pinned->OnMapMeasureRelease();
			}
			return true;
		}
		if (bPortPress && Event.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bPortPress = false;
			UAtlasSubsystem* Atlas = Sub();
			// A press on a port that never moved is an ordinary click on its building.
			if (Atlas && Atlas->ReleaseBeltPress())
			{
				if (const TSharedPtr<SAtlasHud> Pinned = Hud.Pin())
				{
					Pinned->OnMapClick(FVector2f(Event.GetScreenSpacePosition()));
				}
			}
			return true;
		}
		if (SwallowUp.IsValid() && Event.GetEffectingButton() == SwallowUp)
		{
			SwallowUp = FKey();
			if (bDragPaste)
			{
				bDragPaste = false;
				if (const TSharedPtr<SAtlasHud> Pinned = Hud.Pin())
				{
					Pinned->EndDragPaste();
				}
			}
			return true;
		}
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

	/** Drag-paste follows the cursor. Moves are never swallowed: the map never saw the press, so it isn't panning. */
	virtual bool HandleMouseMoveEvent(FSlateApplication&, const FPointerEvent& Event) override
	{
		if (bMeasurePress)
		{
			const TSharedPtr<SAtlasHud> Pinned = Hud.Pin();
			if (!Pinned || !Event.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				// The release went somewhere this didn't see.
				bMeasurePress = false;
				if (Pinned)
				{
					Pinned->OnMapMeasureCancel();
				}
				return false;
			}
			Pinned->OnMapMeasureMove();
			return false;
		}
		if (bPortPress)
		{
			UAtlasSubsystem* Atlas = Sub();
			if (!Atlas || !Atlas->IsBeltPressed() || !Event.IsMouseButtonDown(EKeys::LeftMouseButton))
			{
				// Cancelled, or the release went somewhere this didn't see.
				bPortPress = false;
				if (Atlas)
				{
					Atlas->CancelBeltDrag();
				}
				return false;
			}
			Atlas->MoveBeltPress(FVector2f(Event.GetScreenSpacePosition()));
			return false;
		}
		if (!bDragPaste)
		{
			return false;
		}
		const TSharedPtr<SAtlasHud> Pinned = Hud.Pin();
		if (!Pinned || !Event.IsMouseButtonDown(EKeys::LeftMouseButton))
		{
			// The release went somewhere this didn't see, such as another window.
			bDragPaste = false;
			if (Pinned)
			{
				Pinned->EndDragPaste();
			}
			return false;
		}
		Pinned->OnMapDragPaste(FVector2f(Event.GetScreenSpacePosition()));
		return false;
	}

	/** While a belt drag this processor started is held: R cycles the build mode, T the belt tier, Esc cancels. */
	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& Event) override
	{
		const FKey Key = Event.GetKey();
		if (Key == EKeys::SpaceBar)
		{
			bSpaceHeld = true; // Seen, never swallowed: Space is the game's too
			return false;
		}
		UAtlasSubsystem* Atlas = Sub();
		if (!bPortPress || !Atlas || !Atlas->IsBeltPressed())
		{
			// G toggles measure mode and Esc leaves it, for the map under the cursor.
			const TSharedPtr<SAtlasHud> Pinned = Hud.Pin();
			if (Pinned && Pinned->OnMeasureKey(Event))
			{
				if (Key == EKeys::Escape && bMeasurePress)
				{
					bMeasurePress = false;
				}
				return true;
			}
			return false;
		}
		if (Key == EKeys::Escape)
		{
			Atlas->CancelBeltDrag(); // The release that follows is still swallowed, then ignored
			return true;
		}
		if (Key == EKeys::R)
		{
			Atlas->CycleBeltBuildMode();
			return true;
		}
		if (Key == EKeys::T)
		{
			Atlas->CycleBeltTier();
			return true;
		}
		return false;
	}

	virtual bool HandleKeyUpEvent(FSlateApplication&, const FKeyEvent& Event) override
	{
		if (Event.GetKey() == EKeys::SpaceBar)
		{
			bSpaceHeld = false;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("AtlasClick"); }

private:
	FVector2f DownAt = FVector2f::ZeroVector;
	bool bDown = false;
	/** A left press in measure mode: this processor owns it until release. */
	bool bMeasurePress = false;
	bool bSpaceHeld = false;
	/** A left press that landed on a port: this processor owns it, and the belt drag, until release. */
	bool bPortPress = false;
	/** The button whose release belongs to a swallowed Ctrl-click. */
	FKey SwallowUp;
	bool bDragPaste = false;
};

TSharedRef<SWidget> UAtlasMapHud::MakeWindowHud()
{
	TSharedRef<SAtlasHud> WindowHud = SNew(SAtlasHud).View(EView::Window);
	if (FSlateApplication::IsInitialized())
	{
		// The same click handling as the game's map screen. Both processors see every click; each HUD acts only
		// on clicks over its own map.
		TSharedRef<FAtlasClickProcessor> Processor = MakeShared<FAtlasClickProcessor>();
		Processor->Hud = WindowHud;
		WindowHud->OwnedProcessor = Processor;
		FSlateApplication::Get().RegisterInputPreProcessor(Processor);
	}
	return WindowHud;
}

TSharedRef<SWidget> UAtlasMapHud::RebuildWidget()
{
	Super::RebuildWidget();
	UE_LOG(LogFactoryAtlas, Display, TEXT("[frame %llu] Menu built"), GFrameCounter);
	Hud = SNew(SAtlasHud);
	Hud->Owner = this;
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
	// This runs while the map screen's Slate widgets are being destroyed, so a UMG setter here would write to a
	// slot that is half gone and trip a Slate assert. Put the layout back next tick instead: by then the torn-down
	// widgets have dropped their Slate slots and the setters only store values, while the game UI above the map
	// screen, which stays alive, is restored as usual.
	if (FullscreenUndo.Num() > 0)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Undo = MoveTemp(FullscreenUndo)](float)
		{
			for (int32 i = Undo.Num() - 1; i >= 0; i--)
			{
				Undo[i]();
			}
			return false; // Once
		}));
	}
	FullscreenUndo.Reset();
	FullscreenHidden.Reset();
	FullscreenLogFrames = 0;
	bFullscreenApplied = false;
	if (UAtlasSubsystem* Atlas = Sub())
	{
		Atlas->ClearSelection();
	}
	Super::ReleaseSlateResources(bReleaseChildren);
	Hud.Reset();
}

namespace AtlasFullscreen
{
	/** The widget holding this one: its panel, or past the root of a user widget's tree, that user widget. */
	UWidget* ParentOf(UWidget* Widget)
	{
		if (UPanelWidget* Panel = Widget->GetParent())
		{
			return Panel;
		}
		const UWidgetTree* Tree = Cast<UWidgetTree>(Widget->GetOuter());
		return Tree ? Cast<UUserWidget>(Tree->GetOuter()) : nullptr;
	}

	/** Every widget from Widget up to, not including, Top. Empty if Top is not above it. */
	TArray<UWidget*> PathTo(UWidget* Widget, const UWidget* Top)
	{
		TArray<UWidget*> Path;
		for (UWidget* Current = Widget; Current; Current = ParentOf(Current))
		{
			if (Current == Top)
			{
				return Path;
			}
			Path.Add(Current);
		}
		return {};
	}

	/** Widget, then every widget holding it, up to the one added to the viewport. */
	TArray<UWidget*> Ancestors(UWidget* Widget)
	{
		TArray<UWidget*> Chain;
		for (UWidget* Current = Widget; Current; Current = ParentOf(Current))
		{
			Chain.Add(Current);
		}
		return Chain;
	}

	/** Siblings fullscreen leaves alone: marker popups and the zoom controls. */
	bool KeepVisible(const UWidget* Widget)
	{
		const FString Name = Widget->GetName();
		return Name.Contains(TEXT("Popup")) || Name.Contains(TEXT("MarkerPicker")) || Name.Contains(TEXT("Zoom"));
	}

	template <typename TSlot>
	void FillAligned(TSlot* Slot, TArray<TFunction<void()>>& Undo)
	{
		Undo.Add([Weak = TWeakObjectPtr<TSlot>(Slot), Padding = Slot->GetPadding(),
			H = Slot->GetHorizontalAlignment(), V = Slot->GetVerticalAlignment()]()
		{
			if (TSlot* S = Weak.Get())
			{
				S->SetPadding(Padding);
				S->SetHorizontalAlignment(H);
				S->SetVerticalAlignment(V);
			}
		});
		Slot->SetPadding(FMargin(0.f));
		Slot->SetHorizontalAlignment(HAlign_Fill);
		Slot->SetVerticalAlignment(VAlign_Fill);
	}

	/** A box slot also takes all the room its box has. */
	template <typename TSlot>
	void FillBoxSlot(TSlot* Slot, TArray<TFunction<void()>>& Undo)
	{
		Undo.Add([Weak = TWeakObjectPtr<TSlot>(Slot), Size = Slot->GetSize()]()
		{
			if (TSlot* S = Weak.Get())
			{
				S->SetSize(Size);
			}
		});
		Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		FillAligned(Slot, Undo);
	}
}

void UAtlasMapHud::UpdateFullscreen(bool bWanted)
{
	if (bWanted != bFullscreenApplied)
	{
		if (bWanted)
		{
			ApplyFullscreen();
		}
		else
		{
			RestoreFullscreen();
		}
		return;
	}
	if (FullscreenLogFrames > 0 && --FullscreenLogFrames == 0)
	{
		LogFullscreenChain(TEXT("after"));
	}
	// The game's widgets may show their parts again, for instance when the map screen opens.
	for (const TWeakObjectPtr<UWidget>& Hidden : FullscreenHidden)
	{
		if (UWidget* Widget = Hidden.Get(); Widget && Widget->GetVisibility() != ESlateVisibility::Collapsed)
		{
			Widget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}

void UAtlasMapHud::ApplyFullscreen()
{
	using namespace AtlasFullscreen;
	RestoreFullscreen();
	// Set even when nothing below works out, so a failed attempt is not retried every frame.
	bFullscreenApplied = true;

	// The hook puts this widget in Widget_MapContainer, which holds the map, the filter list and the window frame.
	UUserWidget* Container = GetTypedOuter<UUserWidget>();
	if (!Container || !Container->WidgetTree)
	{
		return;
	}
	UWidget* Map = nullptr;
	Container->WidgetTree->ForEachWidget([&Map](UWidget* Widget)
	{
		if (!Map && Widget->GetClass()->GetName() == TEXT("Widget_Map_C"))
		{
			Map = Widget;
		}
	});
	if (!Map)
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Fullscreen: no Widget_Map inside %s"), *Container->GetName());
		return;
	}

	// The map and this menu stay, along with everything holding them; each holder grows to fill its parent. That
	// goes on above the map screen too, since whatever holds the screen (the game UI, the viewport) may inset it.
	const TArray<UWidget*> Chain = Ancestors(Map);
	if (!Chain.Contains(Container))
	{
		UE_LOG(LogFactoryAtlas, Warning, TEXT("Fullscreen: %s is not inside %s"), *Map->GetName(), *Container->GetName());
		return;
	}
	FullscreenChain.Reset();
	for (UWidget* Widget : Chain)
	{
		FullscreenChain.Add(Widget);
	}
	LogFullscreenChain(TEXT("before"));

	// Only inside the map screen are siblings hidden. Above it they are the rest of the game's UI.
	TArray<UWidget*> Inside = PathTo(Map, Container);
	Inside.Append(PathTo(this, Container));
	const TSet<UWidget*> OnPath(Inside);
	TArray<UWidget*> Path = Inside;
	for (int32 i = Chain.Find(Container); i < Chain.Num(); i++)
	{
		Path.Add(Chain[i]);
	}
	TSet<UWidget*> DoneParents;

	// The widget added to the viewport may have been given a size or position there.
	if (UGameViewportSubsystem* Viewport = UGameViewportSubsystem::Get(GetWorld()); Viewport && Viewport->IsWidgetAdded(Chain.Last()))
	{
		const FGameViewportWidgetSlot Old = Viewport->GetWidgetSlot(Chain.Last());
		FullscreenUndo.Add([Viewport = TWeakObjectPtr<UGameViewportSubsystem>(Viewport), Weak = TWeakObjectPtr<UWidget>(Chain.Last()), Old]()
		{
			if (Viewport.IsValid() && Weak.IsValid() && Viewport->IsWidgetAdded(Weak.Get()))
			{
				Viewport->SetWidgetSlot(Weak.Get(), Old);
			}
		});
		FGameViewportWidgetSlot Full = Old;
		Full.Anchors = FAnchors(0.f, 0.f, 1.f, 1.f);
		Full.Offsets = FMargin(0.f);
		Full.Alignment = FVector2D::ZeroVector;
		Viewport->SetWidgetSlot(Chain.Last(), Full);
	}

	for (UWidget* Widget : Path)
	{
		// A render transform moves a widget after layout, so an offset left by the window's own layout or an
		// animation would push the map past the screen's edge.
		if (!Widget->GetRenderTransform().IsIdentity())
		{
			FullscreenUndo.Add([Weak = TWeakObjectPtr<UWidget>(Widget), Transform = Widget->GetRenderTransform()]()
			{
				if (UWidget* W = Weak.Get())
				{
					W->SetRenderTransform(Transform);
				}
			});
			Widget->SetRenderTransform(FWidgetTransform());
		}

		// A user widget can pad its own content.
		if (UUserWidget* User = Cast<UUserWidget>(Widget); User && User->GetPadding() != FMargin(0.f))
		{
			FullscreenUndo.Add([Weak = TWeakObjectPtr<UUserWidget>(User), Padding = User->GetPadding()]()
			{
				if (UUserWidget* U = Weak.Get())
				{
					U->SetPadding(Padding);
				}
			});
			User->SetPadding(FMargin(0.f));
		}

		UPanelSlot* PathSlot = Widget->Slot;
		if (UCanvasPanelSlot* Canvas = Cast<UCanvasPanelSlot>(PathSlot))
		{
			FullscreenUndo.Add([Weak = TWeakObjectPtr<UCanvasPanelSlot>(Canvas), Layout = Canvas->GetLayout(), bAutoSize = Canvas->GetAutoSize()]()
			{
				if (UCanvasPanelSlot* S = Weak.Get())
				{
					S->SetLayout(Layout);
					S->SetAutoSize(bAutoSize);
				}
			});
			FAnchorData Fill;
			Fill.Offsets = FMargin(0.f);
			Fill.Anchors = FAnchors(0.f, 0.f, 1.f, 1.f);
			Fill.Alignment = FVector2D::ZeroVector;
			Canvas->SetAutoSize(false);
			Canvas->SetLayout(Fill);
		}
		else if (UOverlaySlot* Overlay = Cast<UOverlaySlot>(PathSlot))
		{
			FillAligned(Overlay, FullscreenUndo);
		}
		else if (UHorizontalBoxSlot* HBox = Cast<UHorizontalBoxSlot>(PathSlot))
		{
			FillBoxSlot(HBox, FullscreenUndo);
		}
		else if (UVerticalBoxSlot* VBox = Cast<UVerticalBoxSlot>(PathSlot))
		{
			FillBoxSlot(VBox, FullscreenUndo);
		}
		else if (UBorderSlot* Border = Cast<UBorderSlot>(PathSlot))
		{
			FillAligned(Border, FullscreenUndo);
		}
		else if (USizeBoxSlot* SizeSlot = Cast<USizeBoxSlot>(PathSlot))
		{
			FillAligned(SizeSlot, FullscreenUndo);
		}
		else if (USafeZoneSlot* SafeSlot = Cast<USafeZoneSlot>(PathSlot))
		{
			FillAligned(SafeSlot, FullscreenUndo);
		}
		else if (UGridSlot* GridSlot = Cast<UGridSlot>(PathSlot))
		{
			FillAligned(GridSlot, FullscreenUndo);
		}

		UWidget* Parent = ParentOf(Widget);
		if (!Parent || DoneParents.Contains(Parent))
		{
			continue;
		}
		DoneParents.Add(Parent);

		// A fixed-size box around the map would keep it at window size.
		if (USizeBox* SizeBox = Cast<USizeBox>(Parent))
		{
			FullscreenUndo.Add([Weak = TWeakObjectPtr<USizeBox>(SizeBox),
				bWidth = SizeBox->IsWidthOverride(), Width = SizeBox->GetWidthOverride(),
				bHeight = SizeBox->IsHeightOverride(), Height = SizeBox->GetHeightOverride(),
				bMaxWidth = SizeBox->IsMaxDesiredWidthOverride(), MaxWidth = SizeBox->GetMaxDesiredWidth(),
				bMaxHeight = SizeBox->IsMaxDesiredHeightOverride(), MaxHeight = SizeBox->GetMaxDesiredHeight()]()
			{
				USizeBox* S = Weak.Get();
				if (!S)
				{
					return;
				}
				if (bWidth) { S->SetWidthOverride(Width); }
				if (bHeight) { S->SetHeightOverride(Height); }
				if (bMaxWidth) { S->SetMaxDesiredWidth(MaxWidth); }
				if (bMaxHeight) { S->SetMaxDesiredHeight(MaxHeight); }
			});
			SizeBox->ClearWidthOverride();
			SizeBox->ClearHeightOverride();
			SizeBox->ClearMaxDesiredWidth();
			SizeBox->ClearMaxDesiredHeight();
		}

		// Everything beside the path goes: the window's header and background, the filter list, the tabs.
		// A switcher's other children are pages, not chrome, and it shows only one anyway.
		UPanelWidget* Panel = Cast<UPanelWidget>(Parent);
		if (!OnPath.Contains(Widget) || !Panel || !Panel->CanHaveMultipleChildren() || Panel->IsA<UWidgetSwitcher>())
		{
			continue;
		}
		for (UWidget* Sibling : Panel->GetAllChildren())
		{
			if (!Sibling || OnPath.Contains(Sibling) || KeepVisible(Sibling) || Sibling->GetVisibility() == ESlateVisibility::Collapsed)
			{
				continue;
			}
			FullscreenUndo.Add([Weak = TWeakObjectPtr<UWidget>(Sibling), Visibility = Sibling->GetVisibility()]()
			{
				if (UWidget* W = Weak.Get())
				{
					W->SetVisibility(Visibility);
				}
			});
			FullscreenHidden.Add(Sibling);
			Sibling->SetVisibility(ESlateVisibility::Collapsed);
			UE_LOG(LogFactoryAtlas, Display, TEXT("Fullscreen hides %s (%s) in %s"), *Sibling->GetName(), *Sibling->GetClass()->GetName(), *Panel->GetName());
		}
	}
	UE_LOG(LogFactoryAtlas, Display, TEXT("Fullscreen on: %d changes, %d widgets hidden"), FullscreenUndo.Num(), FullscreenHidden.Num());
	// Layout runs on the next frames; a few in, the sizes are settled.
	FullscreenLogFrames = 3;
}

void UAtlasMapHud::LogFullscreenChain(const TCHAR* When) const
{
	FVector2D ViewportSize = FVector2D::ZeroVector;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(ViewportSize);
	}
	UE_LOG(LogFactoryAtlas, Display, TEXT("Fullscreen layout %s, viewport %.0f x %.0f:"), When, ViewportSize.X, ViewportSize.Y);

	const auto Line = [](const UWidget* Widget, int32 Depth)
	{
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		const FVector2D Position = Geometry.GetAbsolutePosition();
		const FVector2D Size = Geometry.GetAbsoluteSize();
		const FVector2D Shift = Widget->GetRenderTransform().Translation;
		UE_LOG(LogFactoryAtlas, Display, TEXT("  %s%s (%s) slot %s, vis %d, at %.0f,%.0f size %.0f x %.0f, shift %.0f,%.0f"), *FString::ChrN(Depth * 2, TEXT(' ')),
			*Widget->GetName(), *Widget->GetClass()->GetName(), Widget->Slot ? *Widget->Slot->GetClass()->GetName() : TEXT("none"),
			int32(Widget->GetVisibility()), Position.X, Position.Y, Size.X, Size.Y, Shift.X, Shift.Y);
	};
	for (int32 i = FullscreenChain.Num() - 1; i >= 0; i--)
	{
		const UWidget* Widget = FullscreenChain[i].Get();
		if (!Widget)
		{
			continue;
		}
		Line(Widget, 0);
		// The holder's other children, which are what can still take room beside the map.
		const UWidget* OnPath = i > 0 ? FullscreenChain[i - 1].Get() : nullptr;
		if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget); Panel && OnPath && OnPath->GetParent() == Panel)
		{
			for (const UWidget* Child : Panel->GetAllChildren())
			{
				if (Child && Child != OnPath)
				{
					Line(Child, 2);
				}
			}
		}
	}

	// Inside the map itself, a few levels down, in case it keeps its own size.
	const UUserWidget* Map = FullscreenChain.Num() > 0 ? Cast<UUserWidget>(FullscreenChain[0].Get()) : nullptr;
	if (!Map || !Map->WidgetTree || !Map->WidgetTree->RootWidget)
	{
		return;
	}
	TFunction<void(UWidget*, int32)> Down = [&](UWidget* Widget, int32 Depth)
	{
		Line(Widget, Depth);
		const UPanelWidget* Panel = Cast<UPanelWidget>(Widget);
		if (Depth >= 4 || !Panel)
		{
			return;
		}
		for (UWidget* Child : Panel->GetAllChildren())
		{
			if (Child)
			{
				Down(Child, Depth + 1);
			}
		}
	};
	Down(Map->WidgetTree->RootWidget, 1);
}

void UAtlasMapHud::RestoreFullscreen()
{
	bFullscreenApplied = false;
	FullscreenHidden.Reset();
	for (int32 i = FullscreenUndo.Num() - 1; i >= 0; i--)
	{
		FullscreenUndo[i]();
	}
	FullscreenUndo.Reset();
}
