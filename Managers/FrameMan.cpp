#include "FrameMan.h"
#include "PostProcessMan.h"
#include "PrimitiveMan.h"
#include "PerformanceMan.h"
#include "ActivityMan.h"
#include "ConsoleMan.h"
#include "SettingsMan.h"
#include "UInputMan.h"

#include "SLTerrain.h"
#include "Scene.h"

#include "GUI/GUI.h"
#include "GUI/AllegroBitmap.h"
#include "GUI/AllegroScreen.h"

extern bool g_ResetActivity;
extern bool g_InActivity;

namespace RTE {

	const std::string FrameMan::c_ClassName = "FrameMan";

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/// <summary>
	/// Callback function for the allegro set_display_switch_callback. It will be called when focus is switched away from the game window. 
	/// It will temporarily disable positioning of the mouse so that when focus is switched back to the game window, the game window won't fly away because the user clicked the title bar of the window.
	/// </summary>
	void DisplaySwitchOut(void) { g_UInputMan.DisableMouseMoving(true); }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	/// <summary>
	/// Callback function for the allegro set_display_switch_callback. It will be called when focus is switched back to the game window.
	/// </summary>
	void DisplaySwitchIn(void) { g_UInputMan.DisableMouseMoving(false); }

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::Clear() {
		m_GfxDriver = GFX_AUTODETECT_WINDOWED;
		m_ScreenResX = GetSystemMetrics(SM_CXSCREEN);
		m_ScreenResY = GetSystemMetrics(SM_CYSCREEN);
		m_ResX = 960;
		m_ResY = 540;
		m_NewResX = m_ResX;
		m_NewResY = m_ResY;
		m_Fullscreen = false;
		m_ResMultiplier = 1;
		m_HSplit = false;
		m_VSplit = false;
		m_HSplitOverride = false;
		m_VSplitOverride = false;
		m_PlayerScreen = 0;
		m_PlayerScreenWidth = 0;
		m_PlayerScreenHeight = 0;
		m_ScreenDumpBuffer = 0;
		m_WorldDumpBuffer = 0;
		m_BackBuffer8 = 0;
		m_BackBuffer32 = 0;
		m_DrawNetworkBackBuffer = false;
		m_StoreNetworkBackBuffer = false;
		m_NetworkFrameCurrent = 0;
		m_NetworkFrameReady = 1;
		m_PaletteFile.Reset();
		m_BlackColor = 245;
		m_AlmostBlackColor = 245;
		m_PPM = 0;
		m_GUIScreen = 0;
		m_LargeFont = 0;
		m_SmallFont = 0;
		m_TextBlinkTimer.Reset();

		for (short screenCount = 0; screenCount < c_MaxScreenCount; ++screenCount) {
			m_ScreenText[screenCount].clear();
			m_TextDuration[screenCount] = -1;
			m_TextDurationTimer[screenCount].Reset();
			m_TextBlinking[screenCount] = 0;
			m_TextCentered[screenCount] = false;
			m_FlashScreenColor[screenCount] = -1;
			m_FlashedLastFrame[screenCount] = false;
			m_FlashTimer[screenCount].Reset();

			for (short bufferFrame = 0; bufferFrame < 2; bufferFrame++) {
				m_NetworkBackBufferIntermediate8[bufferFrame][screenCount] = 0;
				m_NetworkBackBufferFinal8[bufferFrame][screenCount] = 0;
				m_NetworkBackBufferIntermediateGUI8[bufferFrame][screenCount] = 0;
				m_NetworkBackBufferFinalGUI8[bufferFrame][screenCount] = 0;
			}
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::SetGraphicsDriver() {
		if (g_SettingsMan.ForceOverlayedWindowGfxDriver()) {
			m_GfxDriver = GFX_DIRECTX_OVL;
			g_ConsoleMan.PrintString("SYSTEM: Using overlay DirectX windowed driver!");
		} else if (g_SettingsMan.ForceNonOverlayedWindowGfxDriver()) {
			m_GfxDriver = GFX_DIRECTX_WIN;
			g_ConsoleMan.PrintString("SYSTEM: Using non-overlay DirectX windowed driver!");
		} else if (g_SettingsMan.ForceVirtualFullScreenGfxDriver()) {
			m_GfxDriver = GFX_DIRECTX_WIN_BORDERLESS;
			g_ConsoleMan.PrintString("SYSTEM: Using DirectX fullscreen-windowed driver!");
		} else {
			m_GfxDriver = GFX_AUTODETECT_WINDOWED;
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::ValidateResolution(unsigned short &resX, unsigned short &resY) {
		if (resX * m_ResMultiplier > m_ScreenResX || resY * m_ResMultiplier > m_ScreenResY) {
			allegro_message("Resolution too high to fit display, overriding to fit!");
			resX = m_NewResX = m_ScreenResX / m_ResMultiplier;
			resY = m_NewResY = m_ScreenResY / m_ResMultiplier;
		} else if (resX * m_ResMultiplier == 1366 && resY * m_ResMultiplier == 768) {
			allegro_message("Unfortunately, 1366x768 resolution is not supported by Cortex Command's graphics API. 1360x768 will be used instead!");
			resX = m_NewResX = 1360 / m_ResMultiplier;
			resY = m_NewResY = 768 / m_ResMultiplier;
		} else if ((resX * m_ResMultiplier) % 4 > 0) {
			allegro_message("Resolution width that is not divisible by 4 is not supported!\nOverriding to closest valid width!");
			resX = m_NewResX = std::floor(resX / 4) * 4;
		} else {
			float currentAspectRatio = static_cast<float>(resX) / static_cast<float>(resY);
			if (currentAspectRatio < 1 || currentAspectRatio > 4) {
				allegro_message("Abnormal aspect ratio detected! Reverting to defaults!");
				resX = m_NewResX = 960;
				resY = m_NewResY = 540;
				m_ResMultiplier = m_NewResMultiplier = 1;
			}
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	int FrameMan::Create() {
		SetGraphicsDriver();
		ValidateResolution(m_ResX, m_ResY);
		set_color_depth(m_BPP);

		if (set_gfx_mode(m_GfxDriver, m_ResX * m_ResMultiplier, m_ResY * m_ResMultiplier, 0, 0) != 0) {
			// If a bad resolution somehow slipped past the validation, revert to defaults.
			allegro_message("Unable to set specified graphics mode because: %s!\n\nNow trying to default back to VGA...", allegro_error);
			if (set_gfx_mode(GFX_AUTODETECT_WINDOWED, 960, 540, 0, 0) != 0) {
				set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
				allegro_message("Unable to set any graphics mode because %s!", allegro_error);
				return 1;
			}
			// Successfully reverted to defaults. so set that as the current resolution
			m_ResX = m_NewResX = 960;
			m_ResY = m_NewResY = 540;
			m_ResMultiplier = m_NewResMultiplier = 1;
		}
			
		// Clear the screen buffer so it doesn't flash pink
		clear_to_color(screen, 0);

		// Set the switching mode; what happens when the app window is switched to and from
		set_display_switch_mode(SWITCH_BACKGROUND);
		set_display_switch_callback(SWITCH_OUT, DisplaySwitchOut);
		set_display_switch_callback(SWITCH_IN, DisplaySwitchIn);

		// Sets the allowed color conversions when loading bitmaps from files
		set_color_conversion(COLORCONV_MOST);

		LoadPalette(m_PaletteFile.GetDataPath());

		// Create transparency color table
		PALETTE ccPalette;
		get_palette(ccPalette);
		create_trans_table(&m_LessTransTable, ccPalette, 192, 192, 192, 0);
		create_trans_table(&m_HalfTransTable, ccPalette, 128, 128, 128, 0);
		create_trans_table(&m_MoreTransTable, ccPalette, 64, 64, 64, 0);
		// Set the one Allegro currently uses
		color_map = &m_HalfTransTable;

		CreateBackBuffers();

		return 0;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	int FrameMan::CreateBackBuffers() {
		// Create the back buffer, this is still in 8bpp, we will do any post-processing on the PostProcessing bitmap
		m_BackBuffer8 = create_bitmap_ex(8, m_ResX, m_ResY);
		ClearBackBuffer8();

		// Create the post-processing buffer, it'll be used for glow effects etc
		m_BackBuffer32 = create_bitmap_ex(32, m_ResX, m_ResY);
		ClearBackBuffer32();

		// Create all the network 8bpp back buffers
		for (short i = 0; i < c_MaxScreenCount; i++) {
			for (short f = 0; f < 2; f++) {
				m_NetworkBackBufferIntermediate8[f][i] = create_bitmap_ex(8, m_ResX, m_ResY);
				clear_to_color(m_NetworkBackBufferIntermediate8[f][i], m_BlackColor);
				m_NetworkBackBufferIntermediateGUI8[f][i] = create_bitmap_ex(8, m_ResX, m_ResY);
				clear_to_color(m_NetworkBackBufferIntermediateGUI8[f][i], g_MaskColor);

				m_NetworkBackBufferFinal8[f][i] = create_bitmap_ex(8, m_ResX, m_ResY);
				clear_to_color(m_NetworkBackBufferFinal8[f][i], m_BlackColor);
				m_NetworkBackBufferFinalGUI8[f][i] = create_bitmap_ex(8, m_ResX, m_ResY);
				clear_to_color(m_NetworkBackBufferFinalGUI8[f][i], g_MaskColor);
			}
		}

		m_PlayerScreenWidth = m_BackBuffer8->w;
		m_PlayerScreenHeight = m_BackBuffer8->h;

		// Create the splitscreen buffer
		if (m_HSplit || m_VSplit) {
			m_PlayerScreen = create_bitmap_ex(8, m_ResX / (m_VSplit ? 2 : 1), m_ResY / (m_HSplit ? 2 : 1));
			clear_to_color(m_PlayerScreen, m_BlackColor);
			set_clip_state(m_PlayerScreen, 1);

			// Update these to represent the split screens
			m_PlayerScreenWidth = m_PlayerScreen->w;
			m_PlayerScreenHeight = m_PlayerScreen->h;
		}
		return 0;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	int FrameMan::ReadProperty(std::string propName, Reader &reader) {
		if (propName == "ResolutionX") {
			reader >> m_ResX;
			m_NewResX = m_ResX;
		} else if (propName == "ResolutionY") {
			reader >> m_ResY;
			m_NewResY = m_ResY;
		} else if (propName == "ResolutionMultiplier") {
			reader >> m_ResMultiplier;
		} else if (propName == "HSplitScreen") {
			reader >> m_HSplitOverride;
		} else if (propName == "VSplitScreen") {
			reader >> m_VSplitOverride;
		} else if (propName == "PaletteFile") {
			reader >> m_PaletteFile;
		} else if (propName == "PixelsPerMeter") {
			reader >> m_PPM;
			m_MPP = 1 / m_PPM;
			// Calculate the Pixel per Liter and Liter per Pixel
			float cmPP = 100 / m_PPM;
			float LVolume = 10 * 10 * 10;
			m_PPL = LVolume / (cmPP * cmPP * cmPP);
			m_LPP = 1 / m_PPL;
		} else {
			// See if the base class(es) can find a match instead
			return Serializable::ReadProperty(propName, reader);
		}
		return 0;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	int FrameMan::Save(Writer &writer) const {
		Serializable::Save(writer);

		writer.NewProperty("ResolutionX");
		writer << m_ResX;
		writer.NewProperty("ResolutionY");
		writer << m_ResY;
		writer.NewProperty("HSplitScreen");
		writer << m_HSplitOverride;
		writer.NewProperty("VSplitScreen");
		writer << m_VSplitOverride;
		writer.NewProperty("PaletteFile");
		writer << m_PaletteFile;
		writer.NewProperty("PixelsPerMeter");
		writer << m_PPM;

		return 0;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::Destroy() {
		destroy_bitmap(m_BackBuffer8);
		for (short i = 0; i < c_MaxScreenCount; i++) {
			for (short f = 0; f < 2; f++) {
				destroy_bitmap(m_NetworkBackBufferIntermediate8[f][i]);
				destroy_bitmap(m_NetworkBackBufferIntermediateGUI8[f][i]);
				destroy_bitmap(m_NetworkBackBufferFinal8[f][i]);
				destroy_bitmap(m_NetworkBackBufferFinalGUI8[f][i]);
			}
		}
		destroy_bitmap(m_BackBuffer32);
		destroy_bitmap(m_PlayerScreen);
		destroy_bitmap(m_ScreenDumpBuffer);
		destroy_bitmap(m_WorldDumpBuffer);
		delete m_GUIScreen;
		delete m_LargeFont;
		delete m_SmallFont;

		Clear();
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::Update() {
		g_PerformanceMan.Update();

		// Remove all scheduled primitives, those will be re-added by updates from other entities.
		g_PrimitiveMan.ClearPrimitivesList();
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	bool FrameMan::IsValidResolution(unsigned short width, unsigned short height) const {
		if ((width >= 800 && height >= 600) && (width <= m_ScreenResX || height <= m_ScreenResY)) {
			// Disallow 1366x768 because it's not supported by Allegro.
			if (width == 1366 && height == 768) {
				return false;
			}
			return true;
		} else {
			return false;
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	int FrameMan::SwitchResolutionMultiplier(unsigned char multiplier) {
		if (multiplier <= 0 || multiplier > 4 || multiplier == m_ResMultiplier) {
			return -1;
		}
		if (m_ResX > m_ScreenResX / multiplier || m_ResY > m_ScreenResY / multiplier) {
			allegro_message("Requested resolution multiplier will result in game window exceeding display bounds!\nNo change will be made!");
			return -1;
		}

		// Save the palette so we can re-set it after the switch.
		PALETTE pal;
		get_palette(pal);

		// Need to save these first for recovery attempts to work (screen might be 0)
		unsigned short resX = m_ResX;
		unsigned short resY = m_ResY;

		// Set the GFX_TEXT driver to hack around Allegro's window resizing limitations (specifically reducing window size) when switching from 2X mode to 1X mode.
		// This will force a state where this is no actual game window between multiplier switches and the next set_gfx_mode call will recreate it correctly.
		set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);

		if (set_gfx_mode(m_GfxDriver, resX * multiplier, resY * multiplier, 0, 0) != 0) {
			// Oops, failed to set windowed mode, so go back to previous multiplier
			if (set_gfx_mode(m_GfxDriver, resX * m_ResMultiplier, resY * m_ResMultiplier, 0, 0) != 0) {
				set_gfx_mode(GFX_TEXT, 0, 0, 0, 0);
				allegro_message("Unable to set back to previous windowed mode multiplier because: %s!", allegro_error);
				return 1;
			}
			g_ConsoleMan.PrintString("ERROR: Failed to switch to new windowed mode multiplier, reverted back to previous setting!");
			set_palette(pal);
			return 1;
		}
		g_ConsoleMan.PrintString("SYSTEM: Switched to different windowed mode multiplier.");
		m_ResMultiplier = multiplier;

		Writer settingsWriter("Base.rte/Settings.ini");
		g_SettingsMan.Save(settingsWriter);
		settingsWriter.Destroy();

		set_palette(pal);

		FlipFrameBuffers();
		return 0;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::ResetSplitScreens(bool hSplit, bool vSplit) {
		if (m_PlayerScreen) { release_bitmap(m_PlayerScreen); }

		// Override screen splitting according to settings if needed
		if ((hSplit || vSplit) && !(hSplit && vSplit) && (m_HSplitOverride || m_VSplitOverride)) {
			hSplit = m_HSplitOverride;
			vSplit = m_VSplitOverride;
		}
		m_HSplit = hSplit;
		m_VSplit = vSplit;

		// Create the splitscreen buffer
		if (m_HSplit || m_VSplit) {
			m_PlayerScreen = create_bitmap_ex(8, g_FrameMan.GetResX() / (m_VSplit ? 2 : 1), g_FrameMan.GetResY() / (m_HSplit ? 2 : 1));
			clear_to_color(m_PlayerScreen, m_BlackColor);
			set_clip_state(m_PlayerScreen, 1);

			m_PlayerScreenWidth = m_PlayerScreen->w;
			m_PlayerScreenHeight = m_PlayerScreen->h;
		} else {
			// No splits, so set the screen dimensions equal to the back buffer
			m_PlayerScreenWidth = m_BackBuffer8->w;
			m_PlayerScreenHeight = m_BackBuffer8->h;
		}
		for (short i = 0; i < c_MaxScreenCount; ++i) {
			m_FlashScreenColor[i] = -1;
			m_FlashedLastFrame[i] = false;
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	unsigned short FrameMan::GetPlayerFrameBufferWidth(short whichPlayer) const {
		if (IsInMultiplayerMode()) {
			if (whichPlayer < 0 || whichPlayer >= c_MaxScreenCount) {
				unsigned short width = GetResX();
				for (unsigned short i = 0; i < c_MaxScreenCount; i++) {
					if (m_NetworkBackBufferFinal8[m_NetworkFrameReady][i] && (m_NetworkBackBufferFinal8[m_NetworkFrameReady][i]->w < width)) {
						width = m_NetworkBackBufferFinal8[m_NetworkFrameReady][i]->w;
					}
				}
				return width;
			} else {
				if (m_NetworkBackBufferFinal8[m_NetworkFrameReady][whichPlayer]) {
					return m_NetworkBackBufferFinal8[m_NetworkFrameReady][whichPlayer]->w;
				}
			}
		}
		return m_PlayerScreenWidth;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	unsigned short FrameMan::GetPlayerFrameBufferHeight(short whichPlayer) const {
		if (IsInMultiplayerMode()) {
			if (whichPlayer < 0 || whichPlayer >= c_MaxScreenCount) {
				unsigned short height = GetResY();
				for (unsigned short i = 0; i < c_MaxScreenCount; i++) {
					if (m_NetworkBackBufferFinal8[m_NetworkFrameReady][i] && (m_NetworkBackBufferFinal8[m_NetworkFrameReady][i]->h < height)) { 
						height = m_NetworkBackBufferFinal8[m_NetworkFrameReady][i]->h;
					}
				}
				return height;
			} else {
				if (m_NetworkBackBufferFinal8[m_NetworkFrameReady][whichPlayer]) {
					return m_NetworkBackBufferFinal8[m_NetworkFrameReady][whichPlayer]->h;
				}
			}
		}
		return m_PlayerScreenHeight;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	unsigned short FrameMan::CalculateTextHeight(const std::string &text, unsigned short maxWidth, bool isSmall) {
		return isSmall ? GetSmallFont()->CalculateHeight(text, maxWidth) : GetLargeFont()->CalculateHeight(text, maxWidth);
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	unsigned short FrameMan::CalculateTextWidth(const std::string &text, bool isSmall) {
		return isSmall ? GetSmallFont()->CalculateWidth(text) : GetLargeFont()->CalculateWidth(text);
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::SetScreenText(const std::string &message, short whichScreen, unsigned short blinkInterval, short displayDuration, bool centered) {
		// See if we can overwrite the previous message
		if (whichScreen >= 0 && whichScreen < c_MaxScreenCount && m_TextDurationTimer[whichScreen].IsPastRealMS(m_TextDuration[whichScreen])) {
			m_ScreenText[whichScreen] = message;
			m_TextDuration[whichScreen] = displayDuration;
			m_TextDurationTimer[whichScreen].Reset();
			m_TextBlinking[whichScreen] = blinkInterval;
			m_TextCentered[whichScreen] = centered;
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::ClearScreenText(short whichScreen) {
		if (whichScreen >= 0 && whichScreen < c_MaxScreenCount) {
			m_ScreenText[whichScreen].clear();
			m_TextDuration[whichScreen] = -1;
			m_TextDurationTimer[whichScreen].Reset();
			m_TextBlinking[whichScreen] = 0;
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::FlipFrameBuffers() {
		if (m_ResMultiplier > 1) {
			stretch_blit(m_BackBuffer32, screen, 0, 0, m_BackBuffer32->w, m_BackBuffer32->h, 0, 0, SCREEN_W, SCREEN_H);
		} else {
			blit(m_BackBuffer32, screen, 0, 0, 0, 0, m_BackBuffer32->w, m_BackBuffer32->h);
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::SetTransTable(TransparencyPreset transSetting) {
		switch (transSetting) {
			case LessTrans:
				color_map = &m_LessTransTable;
				break;
			case MoreTrans:
				color_map = &m_MoreTransTable;
				break;
			case HalfTrans:
				color_map = &m_HalfTransTable;
				break;
			default:
				RTEAbort("Undefined transparency preset value passed in. See TransparencyPreset enumeration for defined values.");
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::CreateNewNetworkPlayerBackBuffer(short player, unsigned short width, unsigned short height) {
		for (unsigned short f = 0; f < 2; f++) {
			destroy_bitmap(m_NetworkBackBufferIntermediate8[f][player]);
			m_NetworkBackBufferIntermediate8[f][player] = create_bitmap_ex(8, width, height);

			destroy_bitmap(m_NetworkBackBufferIntermediateGUI8[f][player]);
			m_NetworkBackBufferIntermediateGUI8[f][player] = create_bitmap_ex(8, width, height);

			destroy_bitmap(m_NetworkBackBufferFinal8[f][player]);
			m_NetworkBackBufferFinal8[f][player] = create_bitmap_ex(8, width, height);

			destroy_bitmap(m_NetworkBackBufferFinalGUI8[f][player]);
			m_NetworkBackBufferFinalGUI8[f][player] = create_bitmap_ex(8, width, height);
		}
		m_PlayerScreenWidth = width;
		m_PlayerScreenHeight = height;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	bool FrameMan::LoadPalette(const std::string &palettePath) {
		PALETTE newPalette;
		BITMAP *tempBitmap = load_bitmap(palettePath.c_str(), newPalette);
		RTEAssert(tempBitmap, ("Failed to load palette from bitmap with following path:\n\n" + palettePath).c_str());

		set_palette(newPalette);

		// Update what black is now with the loaded palette
		m_BlackColor = bestfit_color(newPalette, 0, 0, 0);
		m_AlmostBlackColor = bestfit_color(newPalette, 5, 5, 5);

		destroy_bitmap(tempBitmap);

		return true;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	int FrameMan::SaveBitmap(SaveBitmapMode modeToSave, const char *nameBase, BITMAP *bitmapToSave) {
		if (modeToSave == WorldDump && !g_ActivityMan.ActivityRunning()) {
			return 0;
		}

		if (nameBase == 0 || strlen(nameBase) <= 0) {
			return -1;
		}

		unsigned short fileNumber = 0;
		unsigned short maxFileTrys = 1000;
		char fullFileName[256];

		while (fileNumber < maxFileTrys) {
			// Check for the file namebase001.bmp; if it exists, try 002, etc.
			sprintf_s(fullFileName, sizeof(fullFileName), "%s%03i.bmp", nameBase, fileNumber++);
			if (!std::experimental::filesystem::exists(fullFileName)) {
				break;
			}
		}

		PALETTE palette;
		get_palette(palette);

		switch (modeToSave) {
			case SingleBitmap:
				if (bitmapToSave) {
					save_bmp(fullFileName, bitmapToSave, palette);
					g_ConsoleMan.PrintString("SYSTEM: Bitmap was dumped to: " + std::string(fullFileName));
					return 0;
				}
				break;
			case ScreenDump:
				if (screen) {
					if (!m_ScreenDumpBuffer) { m_ScreenDumpBuffer = create_bitmap(screen->w, screen->h); }

					blit(screen, m_ScreenDumpBuffer, 0, 0, 0, 0, screen->w, screen->h);
					save_bmp(fullFileName, m_ScreenDumpBuffer, palette);
					g_ConsoleMan.PrintString("SYSTEM: Screen was dumped to: " + std::string(fullFileName));
					return 0;
				}
				break;
			case WorldDump:
				if (!m_WorldDumpBuffer) {
					m_WorldDumpBuffer = create_bitmap(g_SceneMan.GetSceneWidth(), g_SceneMan.GetSceneHeight());
				} else {
					// Recreate the buffer if the dimensions don't match the current scene.
					if (m_WorldDumpBuffer->w != g_SceneMan.GetSceneWidth() || m_WorldDumpBuffer->h != g_SceneMan.GetSceneHeight()) {
						destroy_bitmap(m_WorldDumpBuffer);
						m_WorldDumpBuffer = create_bitmap(g_SceneMan.GetSceneWidth(), g_SceneMan.GetSceneHeight());
					}
					DrawWorldDump();
					save_bmp(fullFileName, m_WorldDumpBuffer, palette);
					g_ConsoleMan.PrintString("SYSTEM: World was dumped to: " + string(fullFileName));
					return 0;
				}
				break;
			default:
				g_ConsoleMan.PrintString("ERROR: Wrong bitmap save mode passed in, no bitmap was saved!");
				return -1;
		}
		g_ConsoleMan.PrintString("ERROR: Unable to save bitmap to: " + std::string(fullFileName));
		return -1;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	int FrameMan::SharedDrawLine(BITMAP *bitmap, const Vector &start, const Vector &end, unsigned char color, unsigned char altColor, unsigned short skip, unsigned short skipStart, bool shortestWrap, bool drawDot, BITMAP *dot) {
		RTEAssert(bitmap, "Trying to draw line to null Bitmap");
		if (drawDot) { RTEAssert(dot, "Trying to draw line of dots without specifying a dot Bitmap"); }

		int error = 0;
		int dom = 0;
		int sub = 0;
		int domSteps = 0;
		int skipped = skip + (skipStart - skip);
		int intPos[2];
		int delta[2];
		int delta2[2];
		int increment[2];
		bool drawAlt = false;

		unsigned short dotHeight = drawDot ? dot->h : 0;
		unsigned short dotWidth = drawDot ? dot->w : 0;

		//acquire_bitmap(bitmap);

		// Just make the alt the same color as the main one if no one was specified
		if (altColor == 0) { altColor = color; }

		intPos[X] = floorf(start.m_X);
		intPos[Y] = floorf(start.m_Y);

		// Wrap line around the scene if it makes it shorter
		if (shortestWrap) {
			Vector deltaVec = g_SceneMan.ShortestDistance(start, end, false);
			delta[X] = floorf(deltaVec.m_X);
			delta[Y] = floorf(deltaVec.m_Y);
		} else {
			delta[X] = floorf(end.m_X) - intPos[X];
			delta[Y] = floorf(end.m_Y) - intPos[Y];
		}
		if (delta[X] == 0 && delta[Y] == 0) {
			return 0;
		}

		// Bresenham's line drawing algorithm preparation
		if (delta[X] < 0) {
			increment[X] = -1;
			delta[X] = -delta[X];
		} else {
			increment[X] = 1;
		}
		if (delta[Y] < 0) {
			increment[Y] = -1;
			delta[Y] = -delta[Y];
		} else {
			increment[Y] = 1;
		}

		// Scale by 2, for better accuracy of the error at the first pixel
		delta2[X] = delta[X] << 1;
		delta2[Y] = delta[Y] << 1;

		// If X is dominant, Y is submissive, and vice versa.
		if (delta[X] > delta[Y]) {
			dom = X;
			sub = Y;
		} else {
			dom = Y;
			sub = X;
		}
		error = delta2[sub] - delta[dom];

		// Bresenham's line drawing algorithm execution
		for (domSteps = 0; domSteps < delta[dom]; ++domSteps) {
			intPos[dom] += increment[dom];
			if (error >= 0) {
				intPos[sub] += increment[sub];
				error -= delta2[dom];
			}
			error += delta2[sub];

			// Only draw pixel if we're not due to skip any
			if (++skipped > skip) {
				// Scene wrapping, if necessary
				g_SceneMan.WrapPosition(intPos[X], intPos[Y]);

				if (drawDot) {
					masked_blit(dot, bitmap, 0, 0, intPos[X] - (dotWidth / 2), intPos[Y] - (dotHeight / 2), dot->w, dot->h);
				} else {
					putpixel(bitmap, intPos[X], intPos[Y], drawAlt ? color : altColor);
				}
				drawAlt = !drawAlt;
				skipped = 0;
			}
		}

		//release_bitmap(bitmap);

		// Return the end phase state of the skipping
		return skipped;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	GUIFont * FrameMan::GetFont(bool isSmall) {
		if (!m_GUIScreen) { m_GUIScreen = new AllegroScreen(m_BackBuffer8); }

		switch (isSmall) {
			case false:
				if (!m_LargeFont) {
					m_LargeFont = new GUIFont("FatFont");
					m_LargeFont->Load(m_GUIScreen, "Base.rte/GUIs/Skins/Base/fatfont.bmp");
				}
				return m_LargeFont;
			case true:
				if (!m_SmallFont) {
					m_SmallFont = new GUIFont("SmallFont");
					m_SmallFont->Load(m_GUIScreen, "Base.rte/GUIs/Skins/Base/smallfont.bmp");
				}
				return m_SmallFont;
		}
		return 0;
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::UpdateScreenOffsetForSplitScreen(short playerScreen, Vector &screenOffset) {
		switch (playerScreen) {
			case 1:
				// If both splits, or just VSplit, then in upper right quadrant
				if ((m_VSplit && !m_HSplit) || (m_VSplit && m_HSplit)) {
					screenOffset.SetIntXY(GetResX() / 2, 0);
				} else {
					// If only HSplit, then lower left quadrant
					screenOffset.SetIntXY(0, GetResY() / 2);
				}
				break;
			case 2:
				// Always lower left quadrant
				screenOffset.SetIntXY(0, GetResY() / 2);
				break;
			case 3:
				// Always lower right quadrant
				screenOffset.SetIntXY(GetResX() / 2, GetResY() / 2);
				break;
			default:
				// Always upper left corner
				screenOffset.SetIntXY(0, 0);
				break;
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::Draw() {
		// Count how many split screens we'll need
		int screenCount = (m_HSplit ? 2 : 1) * (m_VSplit ? 2 : 1);
		RTEAssert(screenCount <= 1 || m_PlayerScreen, "Splitscreen surface not ready when needed!");

		g_PostProcessMan.ClearScreenPostEffects();

		// These accumulate the effects for each player's screen area, and are then transferred to the post-processing lists with the player screen offset applied
		list<PostEffect> screenRelativeEffects;
		list<Box> screenRelativeGlowBoxes;

		const Activity *pActivity = g_ActivityMan.GetActivity();

		for (int playerScreen = 0; playerScreen < screenCount; ++playerScreen) {
			screenRelativeEffects.clear();
			screenRelativeGlowBoxes.clear();

			BITMAP *drawScreen = (screenCount == 1) ? m_BackBuffer8 : m_PlayerScreen;
			BITMAP *drawScreenGUI = drawScreen;
			if (IsInMultiplayerMode()) {
				drawScreen = m_NetworkBackBufferIntermediate8[m_NetworkFrameCurrent][playerScreen];
				drawScreenGUI = m_NetworkBackBufferIntermediateGUI8[m_NetworkFrameCurrent][playerScreen];
			}
			AllegroBitmap playerGUIBitmap(drawScreenGUI);

			// Update the scene view to line up with a specific screen and then draw it onto the intermediate screen
			g_SceneMan.Update(playerScreen);

			// Save scene layer's offsets for each screen, server will pick them to build the frame state and send to client
			if (IsInMultiplayerMode()) {
				unsigned short layerCount = 0;

				for (const SceneLayer *sceneLayer : g_SceneMan.GetScene()->GetBackLayers()) {
					SLOffset[playerScreen][layerCount] = sceneLayer->GetOffset();
					layerCount++;

					if (layerCount >= c_MaxLayersStoredForNetwork) {
						break;
					}
				}
			}
			Vector targetPos = g_SceneMan.GetOffset(playerScreen);

			// Adjust the drawing position on the target screen for if the target screen is larger than the scene in non-wrapping dimension.
			// Scene needs to be displayed centered on the target bitmap then, and that has to be adjusted for when drawing to the screen
			if (!g_SceneMan.SceneWrapsX() && drawScreen->w > g_SceneMan.GetSceneWidth()) { targetPos.m_X += (drawScreen->w - g_SceneMan.GetSceneWidth()) / 2; }
			if (!g_SceneMan.SceneWrapsY() && drawScreen->h > g_SceneMan.GetSceneHeight()) { targetPos.m_Y += (drawScreen->h - g_SceneMan.GetSceneHeight()) / 2; }

			// Try to move at the frame buffer copy time to maybe prevent wonkyness
			m_TargetPos[m_NetworkFrameCurrent][playerScreen] = targetPos;

			// Draw the scene
			if (!IsInMultiplayerMode()) {
				g_SceneMan.Draw(drawScreen, drawScreenGUI, targetPos);
			} else {
				clear_to_color(drawScreen, g_MaskColor);
				clear_to_color(drawScreenGUI, g_MaskColor);
				g_SceneMan.Draw(drawScreen, drawScreenGUI, targetPos, true, true);
			}

			// Get only the scene-relative post effects that affect this player's screen
			if (pActivity) {
				g_PostProcessMan.GetPostScreenEffectsWrapped(targetPos, drawScreen->w, drawScreen->h, screenRelativeEffects, pActivity->GetTeamOfPlayer(pActivity->PlayerOfScreen(playerScreen)));
				g_PostProcessMan.GetGlowAreasWrapped(targetPos, drawScreen->w, drawScreen->h, screenRelativeGlowBoxes);

				if (IsInMultiplayerMode()) { g_PostProcessMan.SetNetworkPostEffectsList(playerScreen, screenRelativeEffects); }
			}

			// TODO: Find out what keeps disabling the clipping on the draw bitmap
			// Enable clipping on the draw bitmap
			set_clip_state(drawScreen, 1);

			DrawScreenText(playerScreen, playerGUIBitmap);
			
			// The position of the current draw screen on the backbuffer
			Vector screenOffset;

			// If we are dealing with split screens, then deal with the fact that we need to draw the player screens to different locations on the final buffer
			if (screenCount > 1) { UpdateScreenOffsetForSplitScreen(playerScreen, screenOffset); }

			DrawScreenFlash(playerScreen, drawScreenGUI);
			
			if (!IsInMultiplayerMode()) { 
				// Draw the intermediate draw splitscreen to the appropriate spot on the back buffer
				blit(drawScreen, m_BackBuffer8, 0, 0, screenOffset.GetFloorIntX(), screenOffset.GetFloorIntY(), drawScreen->w, drawScreen->h);
		
				g_PostProcessMan.AdjustEffectsPosToPlayerScreen(playerScreen, drawScreen, screenOffset, screenRelativeEffects, screenRelativeGlowBoxes);
			}
		}

		// Clears the pixels that have been revealed from the unseen layers
		g_SceneMan.ClearSeenPixels();

		if (!IsInMultiplayerMode()) {
			// Draw separating lines for split-screens
			if (m_HSplit) {
				hline(m_BackBuffer8, 0, (m_BackBuffer8->h / 2) - 1, m_BackBuffer8->w - 1, m_AlmostBlackColor);
				hline(m_BackBuffer8, 0, (m_BackBuffer8->h / 2), m_BackBuffer8->w - 1, m_AlmostBlackColor);
			}
			if (m_VSplit) {
				vline(m_BackBuffer8, (m_BackBuffer8->w / 2) - 1, 0, m_BackBuffer8->h - 1, m_AlmostBlackColor);
				vline(m_BackBuffer8, (m_BackBuffer8->w / 2), 0, m_BackBuffer8->h - 1, m_AlmostBlackColor);
			}

			// Replace 8 bit backbuffer contents with network received image before post-processing as it is where this buffer is copied to 32 bit buffer
			if (GetDrawNetworkBackBuffer()) {
				m_NetworkBitmapLock[0].lock();

				blit(m_NetworkBackBufferFinal8[m_NetworkFrameReady][0], m_BackBuffer8, 0, 0, 0, 0, m_BackBuffer8->w, m_BackBuffer8->h);
				masked_blit(m_NetworkBackBufferFinalGUI8[m_NetworkFrameReady][0], m_BackBuffer8, 0, 0, 0, 0, m_BackBuffer8->w, m_BackBuffer8->h);

				if (g_UInputMan.FlagAltState() || g_UInputMan.FlagCtrlState() || g_UInputMan.FlagShiftState()) { g_PerformanceMan.DrawCurrentPing(); }

				m_NetworkBitmapLock[0].unlock();
			}
		}

		if (IsInMultiplayerMode()) { PrepareFrameForNetwork(); }

		if (g_InActivity) { g_PostProcessMan.PostProcess(); }

		// Draw the console on top of everything
		g_ConsoleMan.Draw(m_BackBuffer32);

#ifdef DEBUG_BUILD
		// Draw scene seam
		vline(m_BackBuffer8, 0, 0, g_SceneMan.GetSceneHeight(), 5);
#endif

		// Reset the frame timer so we can measure how much it takes until next frame being drawn
		g_PerformanceMan.ResetFrameTimer();
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::DrawScreenText(short playerScreen, AllegroBitmap playerGUIBitmap) {
		int textPosY = 0;
		// Only draw screen text to actual human players
		if (playerScreen < g_ActivityMan.GetActivity()->GetHumanCount()) {
			textPosY += 12;

			if (!m_ScreenText[playerScreen].empty()) {
				unsigned short bufferOrScreenWidth = IsInMultiplayerMode() ? GetPlayerFrameBufferWidth(playerScreen) : GetPlayerScreenWidth();
				unsigned short bufferOrScreenHeight = IsInMultiplayerMode() ? GetPlayerFrameBufferHeight(playerScreen) : GetPlayerScreenHeight();

				if (m_TextCentered[playerScreen]) { textPosY = (bufferOrScreenHeight / 2) - 52; }

				int screenOcclusionOffsetX = g_SceneMan.GetScreenOcclusion(playerScreen).m_X;
				// If there's really no room to offset the text into, then don't
				if (GetPlayerScreenWidth() <= GetResX() / 2) { screenOcclusionOffsetX = 0; }

				// Draw text and handle blinking by turning on and off extra surrounding characters. Text is always drawn to keep it readable.
				if (m_TextBlinking[playerScreen] && m_TextBlinkTimer.AlternateReal(m_TextBlinking[playerScreen])) {
					GetLargeFont()->DrawAligned(&playerGUIBitmap, (bufferOrScreenWidth + screenOcclusionOffsetX) / 2, textPosY, (">>> " + m_ScreenText[playerScreen] + " <<<").c_str(), GUIFont::Centre);
				} else {
					GetLargeFont()->DrawAligned(&playerGUIBitmap, (bufferOrScreenWidth + screenOcclusionOffsetX) / 2, textPosY, m_ScreenText[playerScreen].c_str(), GUIFont::Centre);
				}
				textPosY += 12;
			}

			// Draw info text when in MOID or material layer draw mode
			switch (g_SceneMan.GetLayerDrawMode()) {
				case g_LayerTerrainMatter:
					GetSmallFont()->DrawAligned(&playerGUIBitmap, GetPlayerScreenWidth() / 2, GetPlayerScreenHeight() - 12, "Viewing terrain material layer\nHit Ctrl+M to cycle modes", GUIFont::Centre, GUIFont::Bottom);
					break;
				case g_LayerMOID:
					GetSmallFont()->DrawAligned(&playerGUIBitmap, GetPlayerScreenWidth() / 2, GetPlayerScreenHeight() - 12, "Viewing MovableObject ID layer\nHit Ctrl+M to cycle modes", GUIFont::Centre, GUIFont::Bottom);
					break;
				default:
					break;
			}
			g_PerformanceMan.Draw(playerGUIBitmap);

		} else {
			// If superfluous screen (as in a three-player match), make the fourth the Observer one
			GetLargeFont()->DrawAligned(&playerGUIBitmap, GetPlayerScreenWidth() / 2, textPosY, "- Observer View -", GUIFont::Centre);
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::DrawScreenFlash(short playerScreen, BITMAP *playerGUIBitmap) {
		if (m_FlashScreenColor[playerScreen] != -1) {
			// If set to flash for a period of time, first be solid and then start flashing slower
			float timeTillLimit = m_FlashTimer[playerScreen].LeftTillRealTimeLimitMS();

			if (timeTillLimit < 10 || m_FlashTimer[playerScreen].AlternateReal(50)) {
				if (m_FlashedLastFrame[playerScreen]) {
					m_FlashedLastFrame[playerScreen] = false;
				} else {
					rectfill(playerGUIBitmap, 0, 0, playerGUIBitmap->w, playerGUIBitmap->h, m_FlashScreenColor[playerScreen]);
					m_FlashedLastFrame[playerScreen] = true;
				}
			}
			if (m_FlashTimer[playerScreen].IsPastRealTimeLimit()) { m_FlashScreenColor[playerScreen] = -1; }
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::DrawWorldDump() {
		float worldBitmapWidth = static_cast<float>(m_WorldDumpBuffer->w);
		float worldBitmapHeight = static_cast<float>(m_WorldDumpBuffer->h);
		Vector targetPos(0, 0);

		std::list<PostEffect> postEffectsList;
		BITMAP *effectBitmap = 0;
		int effectPosX = 0;
		int effectPosY = 0;
		unsigned char effectStrength = 0;

		clear_to_color(m_WorldDumpBuffer, makecol32(132, 192, 252)); // Light blue color

		// Draw sky gradient
		for (unsigned int i = 0; i < m_WorldDumpBuffer->h; i++) {
			unsigned int lineColor = makecol32(64 + ((static_cast<float>(i) / worldBitmapHeight) * (128 - 64)), 64 + ((static_cast<float>(i) / worldBitmapHeight) * (192 - 64)), 96 + ((static_cast<float>(i) / worldBitmapHeight) * (255 - 96)));
			hline(m_WorldDumpBuffer, 0, i, worldBitmapWidth - 1, lineColor);
		}

		// Draw scene
		draw_sprite(m_WorldDumpBuffer, g_SceneMan.GetTerrain()->GetBGColorBitmap(), 0, 0);
		draw_sprite(m_WorldDumpBuffer, g_SceneMan.GetTerrain()->GetFGColorBitmap(), 0, 0);

		// Draw objects
		draw_sprite(m_WorldDumpBuffer, g_SceneMan.GetMOColorBitmap(), 0, 0);

		//Draw post-effects
		g_PostProcessMan.GetPostScreenEffectsWrapped(targetPos, worldBitmapWidth, worldBitmapHeight, postEffectsList, -1);

		for (const PostEffect &postEffect : postEffectsList) {
			effectBitmap = postEffect.m_Bitmap;
			effectStrength = postEffect.m_Strength;
			set_screen_blender(effectStrength, effectStrength, effectStrength, effectStrength);
			effectPosX = postEffect.m_Pos.GetFloorIntX() - (effectBitmap->w / 2);
			effectPosY = postEffect.m_Pos.GetFloorIntY() - (effectBitmap->h / 2);

			if (postEffect.m_Angle == 0) {
				draw_trans_sprite(m_WorldDumpBuffer, effectBitmap, effectPosX, effectPosY);
			} else {
				BITMAP *targetBitmap = g_PostProcessMan.GetTempEffectBitmap(effectBitmap);
				clear_to_color(targetBitmap, 0);

				fixed fAngle;
				fAngle = fixmul(postEffect.m_Angle, radtofix_r);

				rotate_sprite(targetBitmap, effectBitmap, 0, 0, fAngle);
				draw_trans_sprite(m_WorldDumpBuffer, targetBitmap, effectPosX, effectPosY);
			}
		}
	}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	void FrameMan::PrepareFrameForNetwork() {
		unsigned short dx = 0;
		unsigned short dy = 0;
		unsigned short dw = m_BackBuffer8->w / 2;
		unsigned short dh = m_BackBuffer8->h / 2;

		// Blit all four internal player screens onto the backbuffer
		for (unsigned short i = 0; i < c_MaxScreenCount; i++) {
			dx = (i == 1 || i == 3) ? dw : dx;
			dy = (i == 2 || i == 3) ? dh : dy;

			m_NetworkBitmapLock[i].lock();
			blit(m_NetworkBackBufferIntermediate8[m_NetworkFrameCurrent][i], m_NetworkBackBufferFinal8[m_NetworkFrameCurrent][i], 0, 0, 0, 0, m_NetworkBackBufferFinal8[m_NetworkFrameCurrent][i]->w, m_NetworkBackBufferFinal8[m_NetworkFrameCurrent][i]->h);
			blit(m_NetworkBackBufferIntermediateGUI8[m_NetworkFrameCurrent][i], m_NetworkBackBufferFinalGUI8[m_NetworkFrameCurrent][i], 0, 0, 0, 0, m_NetworkBackBufferFinalGUI8[m_NetworkFrameCurrent][i]->w, m_NetworkBackBufferFinalGUI8[m_NetworkFrameCurrent][i]->h);
			m_NetworkBitmapLock[i].unlock();

#if defined DEBUG_BUILD || defined MIN_DEBUG_BUILD
			// Draw all player's screen into one
			if (g_UInputMan.KeyHeld(KEY_5)) {
				stretch_blit(m_NetworkBackBufferFinal8[m_NetworkFrameCurrent][i], m_BackBuffer8, 0, 0, m_NetworkBackBufferFinal8[m_NetworkFrameReady][i]->w, m_NetworkBackBufferFinal8[m_NetworkFrameReady][i]->h, dx, dy, dw, dh);
			}
#endif
		}

#if defined DEBUG_BUILD || defined MIN_DEBUG_BUILD
		if (g_UInputMan.KeyHeld(KEY_1)) {
			stretch_blit(m_NetworkBackBufferFinal8[0][0], m_BackBuffer8, 0, 0, m_NetworkBackBufferFinal8[m_NetworkFrameReady][0]->w, m_NetworkBackBufferFinal8[m_NetworkFrameReady][0]->h, 0, 0, m_BackBuffer8->w, m_BackBuffer8->h);
		}
		if (g_UInputMan.KeyHeld(KEY_2)) {
			stretch_blit(m_NetworkBackBufferFinal8[1][0], m_BackBuffer8, 0, 0, m_NetworkBackBufferFinal8[m_NetworkFrameReady][1]->w, m_NetworkBackBufferFinal8[m_NetworkFrameReady][1]->h, 0, 0, m_BackBuffer8->w, m_BackBuffer8->h);
		}
		if (g_UInputMan.KeyHeld(KEY_3)) {
			stretch_blit(m_NetworkBackBufferFinal8[m_NetworkFrameReady][2], m_BackBuffer8, 0, 0, m_NetworkBackBufferFinal8[m_NetworkFrameReady][2]->w, m_NetworkBackBufferFinal8[m_NetworkFrameReady][2]->h, 0, 0, m_BackBuffer8->w, m_BackBuffer8->h);
		}
		if (g_UInputMan.KeyHeld(KEY_4)) {
			stretch_blit(m_NetworkBackBufferFinal8[m_NetworkFrameReady][3], m_BackBuffer8, 0, 0, m_NetworkBackBufferFinal8[m_NetworkFrameReady][3]->w, m_NetworkBackBufferFinal8[m_NetworkFrameReady][3]->h, 0, 0, m_BackBuffer8->w, m_BackBuffer8->h);
		}
#endif
		// Rendering complete, we can finally mark current frame as ready. This is needed to make rendering look totally atomic for the server pulling data in separate threads.
		m_NetworkFrameReady = m_NetworkFrameCurrent;
		m_NetworkFrameCurrent = (m_NetworkFrameCurrent == 0) ? 1 : 0;
	}
}