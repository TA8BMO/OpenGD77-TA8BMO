/*
 * Games for OpenGD77 (TA8BMO Special Edition)
 *
 * Shared game framework: theme-derived palette, persistent high scores, the
 * "Oyunlar" menu and the score screen, plus the games themselves.
 *
 * Design rules followed here:
 *  - No blocking loops. Every game advances from the menu tick using ev->time,
 *    so key scanning, audio and the rest of the firmware keep running.
 *  - No dynamic allocation. All state is fixed-size statics.
 *  - Colours are never hard-coded raw RGB565. They are derived from the active
 *    OpenGD77 theme (background/foreground read back from the theme) and are
 *    pre-inverted when the display runs in inverse video, so day/night/normal/
 *    inverted/custom themes all stay readable.
 */
#include "user_interface/uiGlobals.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiUtilities.h"
#include "user_interface/uiLocalisation.h"
#include "functions/codeplug.h"
#include "functions/sound.h"
#include "functions/settings.h"
#include "functions/ticks.h"
#include "hardware/HX8353E.h"
#include "hardware/EEPROM.h"
#include <string.h>

// Game text follows the radio's language: Turkish, English or Spanish - the same
// rule as the firmware info message and the manual.
#define GT(tr, en, es, ru, de) LANG5((tr), (en), (es), (ru), (de)) // Turkish, English, Spanish, Russian, German

// ---------------------------------------------------------------------------
// Palette - built from the live theme, rebuilt on every screen (re)entry.
// ---------------------------------------------------------------------------
typedef struct
{
	uint16_t background;
	uint16_t foreground;
	uint16_t secondaryText;
	uint16_t border;
	uint16_t selected;
	uint16_t accent;
	uint16_t danger;
	uint16_t success;
	uint16_t warning;
	uint16_t gameObject1;
	uint16_t gameObject2;
	uint16_t gameObject3;
	uint16_t shadow;
	bool     darkBackground;
} GamePalette;

// In CCM RAM, which is not cleared at start-up - safe here because every way into
// the games builds the palette before anything reads it. Main RAM is full to the
// byte, and these 28 bytes are what made room for the channel/zone menu.
static __attribute__((section(".ccmram"))) GamePalette gamePal;

// Turn a plain RGB888 into a platform colour, pre-inverted when the panel is
// running inverse video (the framebuffer keeps day colours in that mode).
static uint16_t gameRGB(uint32_t rgb)
{
	if (settingsIsOptionBitSet(BIT_INVERSE_VIDEO))
	{
		rgb ^= 0x00FFFFFFU;
	}
	return PLATFORM_COLOUR_FORMAT_SWAP_BYTES(RGB888_TO_PLATFORM_COLOUR_FORMAT(rgb));
}

// Rough luminance of a platform colour, used to pick object shades that
// contrast with whatever background the user's theme has.
static bool gameColourIsDark(uint16_t platformColour)
{
	uint16_t c = PLATFORM_COLOUR_FORMAT_SWAP_BYTES(platformColour);
	int r = (int)(((c >> 11) & 0x1FU) << 3);
	int g = (int)(((c >> 5) & 0x3FU) << 2);
	int b = (int)((c & 0x1FU) << 3);

	return ((((r * 30) + (g * 59) + (b * 11)) / 100) < 128);
}

static void gamePaletteBuild(void)
{
	uint16_t fg;
	uint16_t bg;

	// Take the real theme colours for the page itself.
	displayThemeResetToDefault();
	displayGetForegroundAndBackgroundColours(&fg, &bg);

	gamePal.background    = bg;
	gamePal.foreground    = fg;
	gamePal.darkBackground = gameColourIsDark(bg);

	// Object colours: bright set on a dark background, deeper set on a light one,
	// so contrast survives any custom theme.
	if (gamePal.darkBackground)
	{
		gamePal.secondaryText = gameRGB(0xB0B0B0U);
		gamePal.border        = gameRGB(0x808080U);
		gamePal.selected      = gameRGB(0x2060C0U);
		gamePal.accent        = gameRGB(0x30D030U);
		gamePal.danger        = gameRGB(0xFF4040U);
		gamePal.success       = gameRGB(0x40E060U);
		gamePal.warning       = gameRGB(0xFFC020U);
		gamePal.gameObject1   = gameRGB(0x00E0FFU);
		gamePal.gameObject2   = gameRGB(0xFF60FFU);
		gamePal.gameObject3   = gameRGB(0xFFE000U);
		gamePal.shadow        = gameRGB(0x303030U);
	}
	else
	{
		gamePal.secondaryText = gameRGB(0x505050U);
		gamePal.border        = gameRGB(0x404040U);
		gamePal.selected      = gameRGB(0x90C0FFU);
		gamePal.accent        = gameRGB(0x008000U);
		gamePal.danger        = gameRGB(0xC00000U);
		gamePal.success       = gameRGB(0x007020U);
		gamePal.warning       = gameRGB(0xA06000U);
		gamePal.gameObject1   = gameRGB(0x006080U);
		gamePal.gameObject2   = gameRGB(0x900090U);
		gamePal.gameObject3   = gameRGB(0x806000U);
		gamePal.shadow        = gameRGB(0xC0C0C0U);
	}
}

static void gameSetColour(uint16_t fg)
{
	displaySetForegroundAndBackgroundColours(fg, gamePal.background);
}

// displayFillRect() does no clipping at all - it indexes straight into the
// framebuffer, so anything crossing the right edge wraps onto the next scan
// line and shows up as a ghost on the left. Scrolling games must go through
// this instead.
static void gameFillRectClipped(int x, int y, int w, int h)
{
	if (x < 0)
	{
		w += x;
		x = 0;
	}
	if (y < 0)
	{
		h += y;
		y = 0;
	}
	if ((x + w) > DISPLAY_SIZE_X)
	{
		w = (DISPLAY_SIZE_X - x);
	}
	if ((y + h) > DISPLAY_SIZE_Y)
	{
		h = (DISPLAY_SIZE_Y - y);
	}

	if ((w > 0) && (h > 0))
	{
		displayFillRect((int16_t) x, (int16_t) y, (int16_t) w, (int16_t) h, false);
	}
}

static void gameClearScreen(void)
{
	gameSetColour(gamePal.background);
	displayFillRect(0, 0, DISPLAY_SIZE_X, DISPLAY_SIZE_Y, false);
}

// ---------------------------------------------------------------------------
// Persistent high scores - stored in the OpenGD77 custom-data area (the same
// mechanism the themes use), so no codeplug/channel/calibration data is touched.
// ---------------------------------------------------------------------------
#define GAME_DATA_MAGIC    0x384D4147U // "GAM8"
#define GAME_DATA_VERSION  2
#define GAME_DATA_SIZE     320         // fixed forever: the block size cannot change later

// GAME_ID_DINO deliberately sits OUTSIDE highScore[]: its score is stored in a
// field added after the checksum, so blocks written by the earlier build stay
// valid and existing records are not wiped.
enum { GAME_ID_SNAKE = 0, GAME_ID_TETRIS, GAME_ID_BREAKOUT, GAME_ID_2048, GAME_ID_COUNT, GAME_ID_DINO = GAME_ID_COUNT };

// High scores plus a saved-in-progress Tetris game. Wrapped in a union so the
// stored block is always exactly GAME_DATA_SIZE - the custom-data API refuses to
// resize an existing block, so the reserved tail leaves room for later games.
typedef union
{
	struct
	{
		uint32_t magic;
		uint16_t version;
		uint16_t flags;

		uint32_t highScore[GAME_ID_COUNT];

		uint8_t  tetSaved;      // 1 = a Tetris game is stored below
		uint8_t  tetType;
		uint8_t  tetRot;
		uint8_t  tetNext;
		int8_t   tetPX;
		int8_t   tetPY;
		uint16_t tetLevel;
		uint16_t tetLines;
		uint16_t tetPad;
		uint32_t tetScore;
		uint8_t  tetField[20][10];

		uint32_t checksum;

		// Appended after the checksum field so older saved blocks still validate.
		uint32_t dinoHighScore;
	} d;
	uint8_t raw[GAME_DATA_SIZE];
} GameDataBlock;

// Main RAM is nearly full on this build, so the big game buffers live in CCMRAM
// (CPU-only memory - never used for DMA, so this is safe here).
__attribute__((section(".ccmram"))) static GameDataBlock gameData;
static bool gameDataLoaded = false;

static uint32_t gameDataChecksum(void)
{
	uint32_t sum = 0xA5A5A5A5U;

	// Everything except the checksum field itself.
	for (int i = 0; i < (int)(GAME_DATA_SIZE - sizeof(uint32_t)); i++)
	{
		sum = ((sum << 1) | (sum >> 31)) ^ (uint32_t) gameData.raw[i];
	}
	return sum;
}

static void gameDataReset(void)
{
	memset(&gameData, 0, sizeof(gameData));
	gameData.d.magic = GAME_DATA_MAGIC;
	gameData.d.version = GAME_DATA_VERSION;
}

// Storage lives in the emulated EEPROM, exactly where the radio's own settings
// are kept, because that path is known to work. The OpenGD77 custom-data area
// in SPI flash was the obvious home, but it is only usable once the CPS has
// written an "OpenGD77" signature to it - on a radio that never had a boot
// image or theme uploaded there is no signature, and every single save returns
// false without a word. That is why scores survived a session but never a
// power cycle.
//
// Address: settings occupy 0x604B..0x60BB and nothing in the firmware touches
// anything else below the boot block at 0x7518, so this sits in free space in
// the same 4K sector as the settings (one erase covers both).
#define GAME_DATA_EEPROM_ADDR  0x6200

static void gameDataLoad(void)
{
	if (gameDataLoaded)
	{
		return;
	}

	gameDataLoaded = true;

	if (EEPROM_Read(GAME_DATA_EEPROM_ADDR, gameData.raw, GAME_DATA_SIZE))
	{
		uint32_t stored = gameData.d.checksum;

		gameData.d.checksum = 0;
		if ((gameData.d.magic == GAME_DATA_MAGIC) &&
				(gameData.d.version == GAME_DATA_VERSION) &&
				(stored == gameDataChecksum()))
		{
			gameData.d.checksum = stored;
			return; // valid
		}
	}

	// Missing, older format, or corrupt -> safe zeros.
	gameDataReset();
}

static bool gameDataSave(void)
{
	gameData.d.magic = GAME_DATA_MAGIC;
	gameData.d.version = GAME_DATA_VERSION;
	gameData.d.checksum = 0;
	gameData.d.checksum = gameDataChecksum();

	return EEPROM_Write(GAME_DATA_EEPROM_ADDR, gameData.raw, GAME_DATA_SIZE);
}

static uint32_t gameScoreGet(int gameId)
{
	gameDataLoad();
	return ((gameId == GAME_ID_DINO) ? gameData.d.dinoHighScore : gameData.d.highScore[gameId]);
}

// Only writes flash when a genuine new record was made.
static bool gameScoreSubmit(int gameId, uint32_t score)
{
	if (score <= gameScoreGet(gameId))
	{
		return false;
	}

	if (gameId == GAME_ID_DINO)
	{
		gameData.d.dinoHighScore = score;
	}
	else
	{
		gameData.d.highScore[gameId] = score;
	}
	gameDataSave();
	return true;
}

static void gameScoresLoad(void)
{
	gameDataLoad();
}

static void gameScoresReset(void)
{
	gameDataLoad();
	memset(gameData.d.highScore, 0, sizeof(gameData.d.highScore));
	gameData.d.dinoHighScore = 0;
}

static void gameScoresSave(void)
{
	gameDataSave();
}

// ---------------------------------------------------------------------------
// Small xorshift RNG - the firmware has no rand(), and we must not block.
// ---------------------------------------------------------------------------
static uint32_t gameRngState = 0x12345678U;

static uint32_t gameRand(void)
{
	gameRngState ^= (gameRngState << 13);
	gameRngState ^= (gameRngState >> 17);
	gameRngState ^= (gameRngState << 5);
	return gameRngState;
}

static void gameRandSeed(void)
{
	uint32_t t = ticksGetMillis();

	gameRngState ^= (t * 2654435761U) | 1U;
	(void) gameRand();
}

// ---------------------------------------------------------------------------
// Shared game states and screen furniture.
// ---------------------------------------------------------------------------
typedef enum
{
	GAME_STATE_START = 0,
	GAME_STATE_RUNNING,
	GAME_STATE_PAUSED,
	GAME_STATE_GAME_OVER,
	GAME_STATE_EXIT_CONFIRM
} GameState;

#define GAME_HEADER_H   13

static void gameDrawHeader(const char *title, uint32_t score)
{
	char buf[24];

	gameSetColour(gamePal.accent);
	displayPrintAt(2, 1, title, FONT_SIZE_1);

	snprintf(buf, sizeof(buf), "%u", (unsigned) score);
	gameSetColour(gamePal.foreground);
	displayPrintAt((DISPLAY_SIZE_X - (int)(strlen(buf) * 6) - 2), 1, buf, FONT_SIZE_1);

	gameSetColour(gamePal.border);
	displayFillRect(0, (GAME_HEADER_H - 2), DISPLAY_SIZE_X, 1, false);
}

// Centred message panel used for pause / game over / exit confirm.
static void gameDrawPanel(const char *l1, const char *l2, const char *l3, const char *l4, uint16_t titleColour)
{
	const int w = DISPLAY_SIZE_X - 16;
	const int h = 68;
	const int x = 8;
	const int y = (DISPLAY_SIZE_Y - h) / 2;

	gameSetColour(gamePal.background);
	displayFillRect(x, y, w, h, false);
	gameSetColour(gamePal.border);
	displayFillRect(x, y, w, 1, false);
	displayFillRect(x, (y + h - 1), w, 1, false);
	displayFillRect(x, y, 1, h, false);
	displayFillRect((x + w - 1), y, 1, h, false);

	gameSetColour(titleColour);
	displayPrintCentered((y + 6), l1, FONT_SIZE_3);

	gameSetColour(gamePal.foreground);
	if (l2 != NULL)
	{
		displayPrintCentered((y + 24), l2, FONT_SIZE_1);
	}
	if (l3 != NULL)
	{
		displayPrintCentered((y + 36), l3, FONT_SIZE_1);
	}
	gameSetColour(gamePal.secondaryText);
	if (l4 != NULL)
	{
		displayPrintCentered((y + 52), l4, FONT_SIZE_1);
	}
}

// Keep game effects down to roughly the rate a fast typist presses keys, which
// is what the melody driver is actually built for.
//
// Starting a melody makes soundTickMelody() poke the C6000 over SPI0 from
// inside a critical section, with no retry and no return check, while the beep
// task is streaming sample data over that same bus (and that one *does* guard
// its writes and retry them ten times). At normal beep rates the two never
// meet. At physics-tick rates - a fast ball clearing a row of bricks - they
// collide, a C6000 audio register takes a corrupt value, and a second tone runs
// underneath the beeps until the radio is power cycled.
#define GAME_BEEP_MIN_GAP_MS  250

static uint32_t gameLastBeepMs = 0;

static void gameBeep(const int16_t *melody)
{
	// Respect the operator's sound settings - stay silent when beeps are off.
	if (nonVolatileSettings.audioPromptMode >= AUDIO_PROMPT_MODE_BEEP)
	{
		uint32_t now = ticksGetMillis();

		// Someone talking on the channel outranks game noise. Received audio
		// carries on normally while a game is open - the main loop services the
		// squelch whatever menu is showing - but every melody mutes the FM feed
		// through and takes the audio amp for its duration, so beeping over a
		// conversation would chop up the very thing you want to hear.
		if (getAudioAmpStatus() & AUDIO_AMP_MODE_RF)
		{
			return;
		}

		// Never cut a melody short - preemption is what puts the two SPI users
		// on the bus at the same moment.
		if (soundMelodyIsPlaying())
		{
			return;
		}

		// Effect beeps also get a hard floor between them. The rare
		// end-of-game ACK/NACK is not throttled.
		if ((melody == MELODY_KEY_BEEP) && ((now - gameLastBeepMs) < GAME_BEEP_MIN_GAP_MS))
		{
			return;
		}

		gameLastBeepMs = now;
		soundSetMelody((int16_t *) melody);
	}
}

// ---------------------------------------------------------------------------
// "Oyunlar" menu
// ---------------------------------------------------------------------------
static const char * const gamesMenuItems[] =
{
	"1. Yilan",
	"2. Tetris",
	"3. Dinozor",
	"4. Skorlar"
};
static const char * const gamesMenuItemsEN[] =
{
	"1. Snake",
	"2. Tetris",
	"3. Dino",
	"4. Scores"
};
static const char * const gamesMenuItemsES[] =
{
	"1. Serpiente",
	"2. Tetris",
	"3. Dino",
	"4. R\xE9""cords"
};
#define GAMES_MENU_COUNT  (sizeof(gamesMenuItems) / sizeof(gamesMenuItems[0]))
_Static_assert((sizeof(gamesMenuItemsES) == sizeof(gamesMenuItems)), "games menu: Turkish and Spanish lists differ in length");
static const char * const gamesMenuItemsRU[] =
{
	"1. \xC7\xEC\xE5\xE9\xEA\xE0",
	"2. \xD2\xE5\xF2\xF0\xE8\xF1",
	"3. \xC4\xE8\xED\xEE",
	"4. \xD0\xE5\xEA\xEE\xF0\xE4\xFB"
};
_Static_assert((sizeof(gamesMenuItemsRU) == sizeof(gamesMenuItems)), "games menu: Turkish and Russian lists differ in length");
static const char * const gamesMenuItemsDE[] =
{
	"1. Snake",
	"2. Tetris",
	"3. Dino",
	"4. Punkte"
};
_Static_assert((sizeof(gamesMenuItemsDE) == sizeof(gamesMenuItems)), "games menu: Turkish and German lists differ in length");
_Static_assert((sizeof(gamesMenuItemsEN) == sizeof(gamesMenuItems)), "games menu: Turkish and English lists differ in length");

static const int gamesMenuTargets[GAMES_MENU_COUNT] =
{
	UI_GAME_SNAKE, UI_GAME_TETRIS, UI_GAME_DINO, UI_GAME_SCORES // Breakout and 2048 were removed (TA8BMO) to make room in flash
};

static int gamesMenuIndex = 0;

static void gamesMenuDraw(void)
{
	gamePaletteBuild();
	displayClearBuf();
	gameClearScreen();

	gameSetColour(gamePal.accent);
	displayPrintCentered(3, GT("OYUNLAR", "GAMES", "JUEGOS", "\xC8\xC3\xD0\xDB", "SPIELE"), FONT_SIZE_3);
	gameSetColour(gamePal.border);
	displayFillRect(4, 19, (DISPLAY_SIZE_X - 8), 1, false);

	for (int i = 0; i < (int) GAMES_MENU_COUNT; i++)
	{
		// FONT_SIZE_3 is 16px tall, so the row pitch and the highlight have to
		// clear that or the band only covers half of the text.
		int y = 21 + (i * 18);
		bool focused = (i == gamesMenuIndex);

		// Exactly the idiom menuDisplayEntry() uses, rather than a colour of our
		// own: the band takes the theme's selected-entry colour and the text is
		// drawn inverted on top of it, so the selected row reads correctly in
		// every theme instead of coming out white on white.
		if (focused)
		{
			displayThemeApply(THEME_ITEM_BG_MENU_ITEM_SELECTED, THEME_ITEM_BG);
			displayFillRoundRect(4, (y - 1), (DISPLAY_SIZE_X - 8), 18, 2, true);
		}

		displayThemeApply(THEME_ITEM_FG_MENU_ITEM, THEME_ITEM_BG);
		displayPrintCore(10, y, GT(gamesMenuItems[i], gamesMenuItemsEN[i], gamesMenuItemsES[i], gamesMenuItemsRU[i], gamesMenuItemsDE[i]), FONT_SIZE_3, TEXT_ALIGN_LEFT, focused);
	}

	displayThemeResetToDefault();
	displayRender();
}

menuStatus_t menuGames(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		gameScoresLoad();
		gameRandSeed();
		gamesMenuIndex = 0;
		gamesMenuDraw();
		return (MENU_STATUS_LIST_TYPE | MENU_STATUS_SUCCESS);
	}

	if (ev->hasEvent)
	{
		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
		{
			menuSystemPushNewMenu(gamesMenuTargets[gamesMenuIndex]);
			return MENU_STATUS_SUCCESS;
		}

		// DOWN moves down the list, UP moves up. (The menu system remaps the front
		// arrows to KEY_UP/KEY_DOWN and the rotary to KEY_RIGHT/KEY_LEFT here.)
		if (KEYCHECK_SHORTUP(ev->keys, KEY_DOWN) || KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
		{
			gamesMenuIndex = ((gamesMenuIndex + 1) % (int) GAMES_MENU_COUNT);
			gamesMenuDraw();
			return MENU_STATUS_SUCCESS;
		}
		if (KEYCHECK_SHORTUP(ev->keys, KEY_UP) || KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
		{
			gamesMenuIndex = ((gamesMenuIndex == 0) ? ((int) GAMES_MENU_COUNT - 1) : (gamesMenuIndex - 1));
			gamesMenuDraw();
			return MENU_STATUS_SUCCESS;
		}

		// Direct selection with 1..4
		if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
		{
			int n = (ev->keys.key - '1');

			if ((n >= 0) && (n < (int) GAMES_MENU_COUNT))
			{
				gamesMenuIndex = n;
				gamesMenuDraw();
				menuSystemPushNewMenu(gamesMenuTargets[n]);
			}
			return MENU_STATUS_SUCCESS;
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Score screen (menu item 4)
// ---------------------------------------------------------------------------
// Breakout and 2048 are gone; their IDs stay in the enum so the saved scores keep their layout.
#define GAME_SCORE_ROWS 3
static const char * const gameScoreNames[GAME_SCORE_ROWS] = { "Yilan", "Tetris", "Dino" };
static const char * const gameScoreNamesEN[GAME_SCORE_ROWS] = { "Snake", "Tetris", "Dino" };
static const char * const gameScoreNamesES[GAME_SCORE_ROWS] = { "Serpiente", "Tetris", "Dino" };
static const char * const gameScoreNamesRU[GAME_SCORE_ROWS] = { "\xC7\xEC\xE5\xE9\xEA\xE0", "\xD2\xE5\xF2\xF0\xE8\xF1", "\xC4\xE8\xED\xEE" };
static const char * const gameScoreNamesDE[GAME_SCORE_ROWS] = { "Snake", "Tetris", "Dino" };
static const uint8_t gameScoreIds[GAME_SCORE_ROWS] =
		{ GAME_ID_SNAKE, GAME_ID_TETRIS, GAME_ID_DINO };
static bool gameScoresConfirmReset = false;

static void gameScoresDraw(void)
{
	char buf[24];

	gamePaletteBuild();
	displayClearBuf();
	gameClearScreen();

	if (gameScoresConfirmReset)
	{
		gameDrawPanel(GT("SKORLAR", "SCORES", "R\xC9""CORDS", "\xD0\xC5\xCA\xCE\xD0\xC4\xDB", "PUNKTE"), GT("Tum skorlar silinsin mi?", "Delete all scores?", "Borrar los r\xE9""cords?", "\xD3\xE4\xE0\xEB\xE8\xF2\xFC \xF0\xE5\xEA\xEE\xF0\xE4\xFB?", "Alle Punkte l\xF6schen?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2", "Gr\xFCn: Ja   Rot: Nein"), gamePal.danger);
		displayRender();
		return;
	}

	gameSetColour(gamePal.accent);
	displayPrintCentered(4, GT("EN YUKSEK SKORLAR", "HIGH SCORES", "MEJORES R\xC9""CORDS", "\xD0\xC5\xCA\xCE\xD0\xC4\xDB", "BESTENLISTE"), FONT_SIZE_1);
	gameSetColour(gamePal.border);
	displayFillRect(4, 18, (DISPLAY_SIZE_X - 8), 1, false);

	for (int i = 0; i < GAME_SCORE_ROWS; i++)
	{
		int y = 26 + (i * 17);

		gameSetColour(gamePal.foreground);
		displayPrintAt(8, y, GT(gameScoreNames[i], gameScoreNamesEN[i], gameScoreNamesES[i], gameScoreNamesRU[i], gameScoreNamesDE[i]), FONT_SIZE_3);

		snprintf(buf, sizeof(buf), "%u", (unsigned) gameScoreGet(gameScoreIds[i]));
		gameSetColour(gamePal.gameObject3);
		displayPrintAt((DISPLAY_SIZE_X - (int)(strlen(buf) * 8) - 8), y, buf, FONT_SIZE_3);
	}

	gameSetColour(gamePal.secondaryText);
	displayPrintCentered((DISPLAY_SIZE_Y - 12), GT("SK2+# uzun: sifirla", "SK2+# long: reset", "SK2+# largo: borrar", "SK2+# \xE4\xEE\xEB\xE3\xEE: \xF1\xE1\xF0\xEE\xF1", "SK2+# lang: reset"), FONT_SIZE_1);

	displayRender();
}

menuStatus_t menuGameScores(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		gameScoresLoad();
		gameScoresConfirmReset = false;
		gameScoresDraw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if (gameScoresConfirmReset)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				gameScoresReset();
				gameScoresSave();
				gameScoresConfirmReset = false;
				gameBeep(MELODY_ACK_BEEP);
				gameScoresDraw();
				return MENU_STATUS_SUCCESS;
			}
			if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
			{
				gameScoresConfirmReset = false;
				gameScoresDraw();
				return MENU_STATUS_SUCCESS;
			}
			return MENU_STATUS_SUCCESS;
		}

		// Deliberately awkward combination so scores can't be wiped by accident.
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_HASH) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_HASH) == false) &&
				BUTTONCHECK_DOWN(ev, BUTTON_SK2))
		{
			gameScoresConfirmReset = true;
			gameScoresDraw();
			return MENU_STATUS_SUCCESS;
		}

		if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Game 1: Snake
// ---------------------------------------------------------------------------
#define SNAKE_CELL       6
#define SNAKE_AREA_X     2
#define SNAKE_AREA_Y     (GAME_HEADER_H + 1)
#define SNAKE_COLS       ((DISPLAY_SIZE_X - (SNAKE_AREA_X * 2)) / SNAKE_CELL)
#define SNAKE_ROWS       ((DISPLAY_SIZE_Y - SNAKE_AREA_Y - 3) / SNAKE_CELL)
#define SNAKE_MAX_LEN    220
#define SNAKE_START_MS   220
#define SNAKE_MIN_MS     90

typedef struct { uint8_t x, y; } SnakeCell;

__attribute__((section(".ccmram"))) static SnakeCell snakeBody[SNAKE_MAX_LEN];
static int       snakeLen = 3;
static int       snakeDirX = 1, snakeDirY = 0;
static int       snakePendX = 1, snakePendY = 0;
static SnakeCell snakeFood;
static uint32_t  snakeScore = 0;
static int       snakeStepMs = SNAKE_START_MS;
static int       snakeFoodEaten = 0;
static GameState snakeState = GAME_STATE_START;
static uint32_t  snakeLastStep = 0;
static bool      snakeNewRecord = false;
static bool      snakeFoodBlink = false;

static bool snakeCellOnBody(int x, int y)
{
	for (int i = 0; i < snakeLen; i++)
	{
		if ((snakeBody[i].x == x) && (snakeBody[i].y == y))
		{
			return true;
		}
	}
	return false;
}

static void snakePlaceFood(void)
{
	// Bounded search, then a deterministic sweep so a nearly-full board still works.
	for (int tries = 0; tries < 200; tries++)
	{
		int x = (int)(gameRand() % (uint32_t) SNAKE_COLS);
		int y = (int)(gameRand() % (uint32_t) SNAKE_ROWS);

		if (!snakeCellOnBody(x, y))
		{
			snakeFood.x = (uint8_t) x;
			snakeFood.y = (uint8_t) y;
			return;
		}
	}

	for (int y = 0; y < SNAKE_ROWS; y++)
	{
		for (int x = 0; x < SNAKE_COLS; x++)
		{
			if (!snakeCellOnBody(x, y))
			{
				snakeFood.x = (uint8_t) x;
				snakeFood.y = (uint8_t) y;
				return;
			}
		}
	}
	// Board completely full - treat as a win-ish end.
	snakeState = GAME_STATE_GAME_OVER;
}

static void snakeReset(void)
{
	snakeLen = 3;
	snakeDirX = 1;
	snakeDirY = 0;
	snakePendX = 1;
	snakePendY = 0;
	snakeScore = 0;
	snakeStepMs = SNAKE_START_MS;
	snakeFoodEaten = 0;
	snakeNewRecord = false;

	for (int i = 0; i < snakeLen; i++)
	{
		snakeBody[i].x = (uint8_t)((SNAKE_COLS / 2) - i);
		snakeBody[i].y = (uint8_t)(SNAKE_ROWS / 2);
	}
	snakePlaceFood();
}

static void snakeDrawCell(int cx, int cy, uint16_t colour, bool inset)
{
	int x = SNAKE_AREA_X + (cx * SNAKE_CELL);
	int y = SNAKE_AREA_Y + (cy * SNAKE_CELL);

	gameSetColour(colour);
	if (inset)
	{
		displayFillRect((x + 1), (y + 1), (SNAKE_CELL - 2), (SNAKE_CELL - 2), false);
	}
	else
	{
		displayFillRect(x, y, SNAKE_CELL, SNAKE_CELL, false);
	}
}

static void snakeDraw(void)
{
	char buf[24];

	displayClearBuf();
	gameClearScreen();
	gameDrawHeader(GT("YILAN", "SNAKE", "SERPIENTE", "\xC7\xCC\xC5\xC9\xCA\xC0", "SNAKE"), snakeScore);

	// Play area frame
	gameSetColour(gamePal.border);
	{
		int w = (SNAKE_COLS * SNAKE_CELL) + 2;
		int h = (SNAKE_ROWS * SNAKE_CELL) + 2;
		int x = SNAKE_AREA_X - 1;
		int y = SNAKE_AREA_Y - 1;

		displayFillRect(x, y, w, 1, false);
		displayFillRect(x, (y + h - 1), w, 1, false);
		displayFillRect(x, y, 1, h, false);
		displayFillRect((x + w - 1), y, 1, h, false);
	}

	if (snakeState != GAME_STATE_START)
	{
		// Food: blinking inset square so it reads even in mono-ish themes.
		if (snakeFoodBlink)
		{
			snakeDrawCell(snakeFood.x, snakeFood.y, gamePal.danger, false);
		}
		else
		{
			snakeDrawCell(snakeFood.x, snakeFood.y, gamePal.danger, true);
		}

		// Body (inset squares) then head (full square + brighter colour)
		for (int i = snakeLen - 1; i >= 1; i--)
		{
			snakeDrawCell(snakeBody[i].x, snakeBody[i].y, gamePal.gameObject1, true);
		}
		snakeDrawCell(snakeBody[0].x, snakeBody[0].y, gamePal.accent, false);
	}

	switch (snakeState)
	{
		case GAME_STATE_START:
			gameDrawPanel(GT("YILAN", "SNAKE", "SERPIENTE", "\xC7\xCC\xC5\xC9\xCA\xC0", "SNAKE"), GT("2 Yukari  8 Asagi", "2 Up  8 Down", "2 Arriba  8 Abajo", "2 \xC2\xE2\xE5\xF0\xF5  8 \xC2\xED\xE8\xE7", "2 Hoch  8 Runter"), GT("4 Sol  6 Sag   * Duraklat", "4 Left  6 Right  * Pause", "4 Izq.  6 Der.  * Pausa", "4 \xCB\xE5\xE2\xEE  6 \xCF\xF0\xE0\xE2\xEE  * \xCF\xE0\xF3\xE7\xE0", "4 Links 6 Rechts * Pause"),
					GT("Yesil: Basla    Kirmizi: Geri", "Green: Start   Red: Back", "Verde: Jugar   Rojo: Atr\xE1s", "\xC7\xE5\xEB: \xD1\xF2\xE0\xF0\xF2 \xCA\xF0\xE0\xF1: \xCD\xE0\xE7\xE0\xE4", "Gr\xFCn: Start Rot: Zur\xFC""ck"), gamePal.accent);
			break;

		case GAME_STATE_PAUSED:
			gameDrawPanel(GT("DURAKLATILDI", "PAUSED", "PAUSA", "\xCF\xC0\xD3\xC7\xC0", "PAUSE"), NULL, GT("* veya Yesil: Devam", "* or Green: Resume", "* o Verde: Seguir", "* \xE8\xEB\xE8 \xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5", "* oder Gr\xFCn: Weiter"), GT("Kirmizi: Cikis", "Red: Exit", "Rojo: Salir", "\xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4", "Rot: Ende"), gamePal.warning);
			break;

		case GAME_STATE_GAME_OVER:
			snprintf(buf, sizeof(buf), GT("Puan: %u", "Score: %u", "Puntos: %u", "\xCE\xF7\xEA\xE8: %u", "Punkte: %u"), (unsigned) snakeScore);
			{
				char buf2[24];
				snprintf(buf2, sizeof(buf2), GT("Rekor: %u", "Best: %u", "M\xE1x: %u", "\xD0\xE5\xEA\xEE\xF0\xE4: %u", "Best: %u"), (unsigned) gameScoreGet(GAME_ID_SNAKE));
				gameDrawPanel((snakeNewRecord ? GT("YENI REKOR", "NEW RECORD", "R\xC9""CORD!", "\xD0\xC5\xCA\xCE\xD0\xC4!", "REKORD!") : GT("OYUN BITTI", "GAME OVER", "FIN DEL JUEGO", "\xCA\xCE\xCD\xC5\xD6 \xC8\xC3\xD0\xDB", "GAME OVER")), buf, buf2,
						GT("Yesil: Tekrar   Kirmizi: Cikis", "Green: Again   Red: Exit", "Verde: Otra  Rojo: Salir", "\xC7\xE5\xEB: \xC5\xF9\xB8  \xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4", "Gr\xFCn: Neu  Rot: Ende"), (snakeNewRecord ? gamePal.success : gamePal.danger));
			}
			break;

		case GAME_STATE_EXIT_CONFIRM:
			gameDrawPanel(GT("CIKIS", "EXIT", "SALIR", "\xC2\xDB\xD5\xCE\xC4", "ENDE"), GT("Oyundan cikilsin mi?", "Leave the game?", "Salir del juego?", "\xC2\xFB\xE9\xF2\xE8 \xE8\xE7 \xE8\xE3\xF0\xFB?", "Spiel verlassen?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2", "Gr\xFCn: Ja   Rot: Nein"), gamePal.warning);
			break;

		default:
			break;
	}

	displayRender();
}

static void snakeStep(void)
{
	int nx;
	int ny;

	// Apply the queued direction (never a direct reverse)
	snakeDirX = snakePendX;
	snakeDirY = snakePendY;

	nx = snakeBody[0].x + snakeDirX;
	ny = snakeBody[0].y + snakeDirY;

	if ((nx < 0) || (nx >= SNAKE_COLS) || (ny < 0) || (ny >= SNAKE_ROWS))
	{
		snakeState = GAME_STATE_GAME_OVER;
		snakeNewRecord = gameScoreSubmit(GAME_ID_SNAKE, snakeScore);
		gameBeep(snakeNewRecord ? MELODY_ACK_BEEP : MELODY_NACK_BEEP);
		return;
	}

	// Self collision (the tail cell is freed this step unless we grow)
	for (int i = 0; i < (snakeLen - 1); i++)
	{
		if ((snakeBody[i].x == nx) && (snakeBody[i].y == ny))
		{
			snakeState = GAME_STATE_GAME_OVER;
			snakeNewRecord = gameScoreSubmit(GAME_ID_SNAKE, snakeScore);
			gameBeep(snakeNewRecord ? MELODY_ACK_BEEP : MELODY_NACK_BEEP);
			return;
		}
	}

	{
		bool ate = ((nx == snakeFood.x) && (ny == snakeFood.y));

		if (ate && (snakeLen < SNAKE_MAX_LEN))
		{
			snakeLen++;
		}

		for (int i = snakeLen - 1; i > 0; i--)
		{
			snakeBody[i] = snakeBody[i - 1];
		}
		snakeBody[0].x = (uint8_t) nx;
		snakeBody[0].y = (uint8_t) ny;

		if (ate)
		{
			int levelBonus = ((SNAKE_START_MS - snakeStepMs) / 20);

			// Guarded so the score can never wrap.
			if (snakeScore < (0xFFFFFFFFU - 100U))
			{
				snakeScore += (uint32_t)(10 + levelBonus);
			}

			snakeFoodEaten++;
			if (((snakeFoodEaten % 4) == 0) && (snakeStepMs > SNAKE_MIN_MS))
			{
				snakeStepMs -= 12;
				if (snakeStepMs < SNAKE_MIN_MS)
				{
					snakeStepMs = SNAKE_MIN_MS;
				}
			}
			gameBeep(MELODY_KEY_BEEP);
			snakePlaceFood();
		}
	}
}

menuStatus_t menuGameSnake(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		gamePaletteBuild();
		gameScoresLoad();
		gameRandSeed();
		snakeReset();
		snakeState = GAME_STATE_START;
		snakeLastStep = ticksGetMillis();
		snakeDraw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		// Long RED always leaves, from any state.
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		switch (snakeState)
		{
			case GAME_STATE_START:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					snakeState = GAME_STATE_RUNNING;
					snakeLastStep = ev->time;
					snakeDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_RUNNING:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
				{
					snakeState = GAME_STATE_PAUSED;
					snakeDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					snakeState = GAME_STATE_EXIT_CONFIRM;
					snakeDraw();
					return MENU_STATUS_SUCCESS;
				}
				// Direction keys - a direct reverse is ignored.
				if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
				{
					switch (ev->keys.key)
					{
						case '2': if (snakeDirY == 0) { snakePendX = 0;  snakePendY = -1; } break;
						case '8': if (snakeDirY == 0) { snakePendX = 0;  snakePendY = 1;  } break;
						case '4': if (snakeDirX == 0) { snakePendX = -1; snakePendY = 0;  } break;
						case '6': if (snakeDirX == 0) { snakePendX = 1;  snakePendY = 0;  } break;
						default: break;
					}
					return MENU_STATUS_SUCCESS;
				}
				break;

			case GAME_STATE_PAUSED:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					snakeState = GAME_STATE_RUNNING;
					snakeLastStep = ev->time;
					snakeDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_GAME_OVER:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					snakeReset();
					snakeState = GAME_STATE_RUNNING;
					snakeLastStep = ev->time;
					snakeDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_EXIT_CONFIRM:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					menuSystemPopPreviousMenu();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					snakeState = GAME_STATE_RUNNING;
					snakeLastStep = ev->time;
					snakeDraw();
				}
				return MENU_STATUS_SUCCESS;
		}
	}

	// Non-blocking tick. Overflow-safe comparison via unsigned subtraction.
	if (snakeState == GAME_STATE_RUNNING)
	{
		if ((uint32_t)(ev->time - snakeLastStep) >= (uint32_t) snakeStepMs)
		{
			snakeLastStep = ev->time;
			snakeFoodBlink = !snakeFoodBlink;
			snakeStep();
			snakeDraw();
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Game 2: Tetris
// 10x20 well on the left, info panel on the right. 4x4 bit masks per rotation.
// ---------------------------------------------------------------------------
#define TET_COLS     10
#define TET_ROWS     20
#define TET_CELL     5
#define TET_X0       4
#define TET_Y0       16
#define TET_PANEL_X  (TET_X0 + (TET_COLS * TET_CELL) + 6)

// bit n of the mask -> row n/4, col n%4
static const uint16_t tetShapes[7][4] =
{
	{ 0x00F0, 0x4444, 0x00F0, 0x4444 }, // I
	{ 0x0066, 0x0066, 0x0066, 0x0066 }, // O
	{ 0x0072, 0x0262, 0x0270, 0x0232 }, // T
	{ 0x0036, 0x0462, 0x0036, 0x0462 }, // S
	{ 0x0063, 0x0264, 0x0063, 0x0264 }, // Z
	{ 0x0071, 0x0226, 0x0470, 0x0322 }, // J
	{ 0x0074, 0x0622, 0x0170, 0x0223 }  // L
};

__attribute__((section(".ccmram"))) static uint8_t tetField[TET_ROWS][TET_COLS];
static int       tetType, tetRot, tetX, tetY;
static int       tetNextType;
static uint8_t   tetBag[7];
static int       tetBagIndex = 7;
static uint32_t  tetScore = 0;
static int       tetLevel = 1;
static int       tetLines = 0;
static int       tetDropMs = 800;
static uint32_t  tetLastDrop = 0;
static GameState tetState = GAME_STATE_START;
static bool      tetNewRecord = false;

static uint16_t tetPieceColour(int type)
{
	switch (type)
	{
		case 0:  return gamePal.gameObject1; // I
		case 1:  return gamePal.gameObject3; // O
		case 2:  return gamePal.gameObject2; // T
		case 3:  return gamePal.success;     // S
		case 4:  return gamePal.danger;      // Z
		case 5:  return gamePal.accent;      // J
		default: return gamePal.warning;     // L
	}
}

static int tetNextFromBag(void)
{
	if (tetBagIndex >= 7)
	{
		for (int i = 0; i < 7; i++)
		{
			tetBag[i] = (uint8_t) i;
		}
		for (int i = 6; i > 0; i--) // Fisher-Yates
		{
			int j = (int)(gameRand() % (uint32_t)(i + 1));
			uint8_t t = tetBag[i];

			tetBag[i] = tetBag[j];
			tetBag[j] = t;
		}
		tetBagIndex = 0;
	}
	return (int) tetBag[tetBagIndex++];
}

// true when the shape at (px,py,rot) does not overlap a wall, the floor or a locked cell
static bool tetFits(int type, int rot, int px, int py)
{
	uint16_t mask = tetShapes[type][rot];

	for (int b = 0; b < 16; b++)
	{
		if ((mask & (1U << b)) == 0)
		{
			continue;
		}

		int cx = px + (b % 4);
		int cy = py + (b / 4);

		if ((cx < 0) || (cx >= TET_COLS) || (cy >= TET_ROWS))
		{
			return false;
		}
		if ((cy >= 0) && (tetField[cy][cx] != 0))
		{
			return false;
		}
	}
	return true;
}

static void tetSpawn(void)
{
	tetType = tetNextType;
	tetNextType = tetNextFromBag();
	tetRot = 0;
	tetX = 3;
	tetY = -1;

	if (!tetFits(tetType, tetRot, tetX, tetY))
	{
		tetState = GAME_STATE_GAME_OVER;
		gameDataLoad();
		gameData.d.tetSaved = 0; // a finished game must never be resumed
		tetNewRecord = gameScoreSubmit(GAME_ID_TETRIS, tetScore);
		gameBeep(tetNewRecord ? MELODY_ACK_BEEP : MELODY_NACK_BEEP);
	}
}

static void tetReset(void)
{
	memset(tetField, 0, sizeof(tetField));
	tetScore = 0;
	tetLevel = 1;
	tetLines = 0;
	tetDropMs = 800;
	tetBagIndex = 7;
	tetNewRecord = false;
	tetNextType = tetNextFromBag();
	tetSpawn();
}

static void tetClearLines(void)
{
	int cleared = 0;

	for (int y = TET_ROWS - 1; y >= 0; y--)
	{
		bool full = true;

		for (int x = 0; x < TET_COLS; x++)
		{
			if (tetField[y][x] == 0)
			{
				full = false;
				break;
			}
		}

		if (full)
		{
			for (int yy = y; yy > 0; yy--)
			{
				memcpy(tetField[yy], tetField[yy - 1], TET_COLS);
			}
			memset(tetField[0], 0, TET_COLS);
			cleared++;
			y++; // re-test this row
		}
	}

	if (cleared > 0)
	{
		static const uint32_t lineScore[5] = { 0, 100, 300, 500, 800 };

		if (tetScore < (0xFFFFFFFFU - 100000U))
		{
			tetScore += (lineScore[cleared] * (uint32_t) tetLevel);
		}
		tetLines += cleared;
		tetLevel = 1 + (tetLines / 10);
		tetDropMs = 800 - ((tetLevel - 1) * 70);
		if (tetDropMs < 100)
		{
			tetDropMs = 100;
		}
		gameBeep(MELODY_ACK_BEEP);
	}
}

static void tetLock(void)
{
	uint16_t mask = tetShapes[tetType][tetRot];

	for (int b = 0; b < 16; b++)
	{
		if ((mask & (1U << b)) == 0)
		{
			continue;
		}

		int cx = tetX + (b % 4);
		int cy = tetY + (b / 4);

		if ((cy >= 0) && (cy < TET_ROWS) && (cx >= 0) && (cx < TET_COLS))
		{
			tetField[cy][cx] = (uint8_t)(tetType + 1);
		}
	}

	tetClearLines();
	tetSpawn();
}

static void tetRotate(void)
{
	int nr = ((tetRot + 1) & 3);
	static const int kick[5] = { 0, -1, 1, -2, 2 }; // simple, safe wall kick

	for (int k = 0; k < 5; k++)
	{
		if (tetFits(tetType, nr, (tetX + kick[k]), tetY))
		{
			tetX += kick[k];
			tetRot = nr;
			return;
		}
	}
}

static void tetDrawCell(int cx, int cy, uint16_t colour, bool outlineOnly)
{
	int x = TET_X0 + (cx * TET_CELL);
	int y = TET_Y0 + (cy * TET_CELL);

	if (cy < 0)
	{
		return;
	}

	gameSetColour(colour);
	if (outlineOnly)
	{
		displayFillRect(x, y, TET_CELL, 1, false);
		displayFillRect(x, (y + TET_CELL - 1), TET_CELL, 1, false);
		displayFillRect(x, y, 1, TET_CELL, false);
		displayFillRect((x + TET_CELL - 1), y, 1, TET_CELL, false);
	}
	else
	{
		displayFillRect(x, y, TET_CELL, TET_CELL, false);
		// inner notch keeps pieces distinguishable without relying on colour alone
		gameSetColour(gamePal.background);
		displayFillRect((x + TET_CELL - 2), (y + TET_CELL - 2), 1, 1, false);
	}
}

static void tetDraw(void)
{
	char buf[20];

	displayClearBuf();
	gameClearScreen();
	gameDrawHeader("TETRIS", tetScore);

	// Well frame
	gameSetColour(gamePal.border);
	{
		int w = (TET_COLS * TET_CELL) + 2;
		int h = (TET_ROWS * TET_CELL) + 2;

		displayFillRect((TET_X0 - 1), (TET_Y0 - 1), w, 1, false);
		displayFillRect((TET_X0 - 1), (TET_Y0 - 1 + h - 1), w, 1, false);
		displayFillRect((TET_X0 - 1), (TET_Y0 - 1), 1, h, false);
		displayFillRect((TET_X0 - 1 + w - 1), (TET_Y0 - 1), 1, h, false);
	}

	// Locked cells
	for (int y = 0; y < TET_ROWS; y++)
	{
		for (int x = 0; x < TET_COLS; x++)
		{
			if (tetField[y][x] != 0)
			{
				tetDrawCell(x, y, tetPieceColour(tetField[y][x] - 1), false);
			}
		}
	}

	if (tetState != GAME_STATE_START)
	{
		uint16_t mask = tetShapes[tetType][tetRot];

		// Ghost: where the piece would land
		{
			int gy = tetY;

			while (tetFits(tetType, tetRot, tetX, (gy + 1)))
			{
				gy++;
			}
			for (int b = 0; b < 16; b++)
			{
				if (mask & (1U << b))
				{
					tetDrawCell((tetX + (b % 4)), (gy + (b / 4)), gamePal.shadow, true);
				}
			}
		}

		for (int b = 0; b < 16; b++)
		{
			if (mask & (1U << b))
			{
				tetDrawCell((tetX + (b % 4)), (tetY + (b / 4)), tetPieceColour(tetType), false);
			}
		}
	}

	// Info panel
	gameSetColour(gamePal.secondaryText);
	displayPrintAt(TET_PANEL_X, 18, GT("Puan", "Score", "Ptos.", "\xCE\xF7\xEA\xE8", "Punkte"), FONT_SIZE_1);
	displayPrintAt(TET_PANEL_X, 40, GT("Seviye", "Level", "Nivel", "\xD3\xF0.", "Level"), FONT_SIZE_1);
	displayPrintAt(TET_PANEL_X, 62, GT("Satir", "Lines", "Filas", "\xD0\xFF\xE4\xFB", "Reihen"), FONT_SIZE_1);
	displayPrintAt(TET_PANEL_X, 84, GT("Sonraki", "Next", "Pr\xF3xima", "\xC4\xE0\xEB\xE5\xE5", "N\xE4""chste"), FONT_SIZE_1);

	gameSetColour(gamePal.foreground);
	snprintf(buf, sizeof(buf), "%u", (unsigned) tetScore);
	displayPrintAt(TET_PANEL_X, 28, buf, FONT_SIZE_1);
	snprintf(buf, sizeof(buf), "%d", tetLevel);
	displayPrintAt(TET_PANEL_X, 50, buf, FONT_SIZE_1);
	snprintf(buf, sizeof(buf), "%d", tetLines);
	displayPrintAt(TET_PANEL_X, 72, buf, FONT_SIZE_1);

	// Next piece preview
	{
		uint16_t nmask = tetShapes[tetNextType][0];

		for (int b = 0; b < 16; b++)
		{
			if (nmask & (1U << b))
			{
				int x = TET_PANEL_X + ((b % 4) * TET_CELL);
				int y = 94 + ((b / 4) * TET_CELL);

				gameSetColour(tetPieceColour(tetNextType));
				displayFillRect(x, y, TET_CELL, TET_CELL, false);
			}
		}
	}

	switch (tetState)
	{
		case GAME_STATE_START:
			gameDrawPanel("TETRIS", GT("4/6 Sol-Sag  2 Dondur", "4/6 Move  2 Rotate", "4/6 Mover  2 Girar", "4/6 \xD1\xE4\xE2\xE8\xE3  2 \xCF\xEE\xE2\xEE\xF0\xEE\xF2", "4/6 Bewegen 2 Drehen"), GT("8 Indir  0 Birak  * Dur", "8/0 Drop     * Pause", "8/0 Soltar   * Pausa", "8/0 \xD1\xE1\xF0\xEE\xF1   * \xCF\xE0\xF3\xE7\xE0", "8/0 Fallen   * Pause"),
					GT("Yesil: Basla   Kirmizi: Geri", "Green: Start   Red: Back", "Verde: Jugar   Rojo: Atr\xE1s", "\xC7\xE5\xEB: \xD1\xF2\xE0\xF0\xF2 \xCA\xF0\xE0\xF1: \xCD\xE0\xE7\xE0\xE4", "Gr\xFCn: Start Rot: Zur\xFC""ck"), gamePal.accent);
			break;

		case GAME_STATE_PAUSED:
			gameDrawPanel(GT("DURAKLATILDI", "PAUSED", "PAUSA", "\xCF\xC0\xD3\xC7\xC0", "PAUSE"), NULL, GT("* veya Yesil: Devam", "* or Green: Resume", "* o Verde: Seguir", "* \xE8\xEB\xE8 \xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5", "* oder Gr\xFCn: Weiter"), GT("Kirmizi: Cikis", "Red: Exit", "Rojo: Salir", "\xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4", "Rot: Ende"), gamePal.warning);
			break;

		case GAME_STATE_GAME_OVER:
			{
				char b2[20];

				snprintf(buf, sizeof(buf), GT("Puan: %u", "Score: %u", "Puntos: %u", "\xCE\xF7\xEA\xE8: %u", "Punkte: %u"), (unsigned) tetScore);
				snprintf(b2, sizeof(b2), GT("Rekor: %u", "Best: %u", "M\xE1x: %u", "\xD0\xE5\xEA\xEE\xF0\xE4: %u", "Best: %u"), (unsigned) gameScoreGet(GAME_ID_TETRIS));
				gameDrawPanel((tetNewRecord ? GT("YENI REKOR", "NEW RECORD", "R\xC9""CORD!", "\xD0\xC5\xCA\xCE\xD0\xC4!", "REKORD!") : GT("OYUN BITTI", "GAME OVER", "FIN DEL JUEGO", "\xCA\xCE\xCD\xC5\xD6 \xC8\xC3\xD0\xDB", "GAME OVER")), buf, b2,
						GT("Yesil: Tekrar   Kirmizi: Cikis", "Green: Again   Red: Exit", "Verde: Otra  Rojo: Salir", "\xC7\xE5\xEB: \xC5\xF9\xB8  \xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4", "Gr\xFCn: Neu  Rot: Ende"), (tetNewRecord ? gamePal.success : gamePal.danger));
			}
			break;

		case GAME_STATE_EXIT_CONFIRM:
			gameDrawPanel(GT("CIKIS", "EXIT", "SALIR", "\xC2\xDB\xD5\xCE\xC4", "ENDE"), GT("Oyundan cikilsin mi?", "Leave the game?", "Salir del juego?", "\xC2\xFB\xE9\xF2\xE8 \xE8\xE7 \xE8\xE3\xF0\xFB?", "Spiel verlassen?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2", "Gr\xFCn: Ja   Rot: Nein"), gamePal.warning);
			break;

		default:
			break;
	}

	displayRender();
}

// Store the game in progress so it survives leaving the screen and a power cycle.
// Written at the few points where the player stops playing (pause / exit / game
// over), never every frame, so flash wear stays negligible.
static void tetSaveState(void)
{
	gameDataLoad();

	if ((tetState == GAME_STATE_RUNNING) || (tetState == GAME_STATE_PAUSED) ||
			(tetState == GAME_STATE_EXIT_CONFIRM))
	{
		gameData.d.tetSaved = 1;
		gameData.d.tetType  = (uint8_t) tetType;
		gameData.d.tetRot   = (uint8_t) tetRot;
		gameData.d.tetNext  = (uint8_t) tetNextType;
		gameData.d.tetPX    = (int8_t) tetX;
		gameData.d.tetPY    = (int8_t) tetY;
		gameData.d.tetLevel = (uint16_t) tetLevel;
		gameData.d.tetLines = (uint16_t) tetLines;
		gameData.d.tetScore = tetScore;
		memcpy(gameData.d.tetField, tetField, sizeof(tetField));
	}
	else if (gameData.d.tetSaved == 0)
	{
		return; // nothing to store and nothing stale to clear - skip the write
	}
	else
	{
		gameData.d.tetSaved = 0;
	}

	gameDataSave();
}

static bool tetRestoreState(void)
{
	gameDataLoad();

	if (gameData.d.tetSaved == 0)
	{
		return false;
	}

	tetType     = gameData.d.tetType;
	tetRot      = gameData.d.tetRot;
	tetNextType = gameData.d.tetNext;
	tetX        = gameData.d.tetPX;
	tetY        = gameData.d.tetPY;
	tetLevel    = gameData.d.tetLevel;
	tetLines    = gameData.d.tetLines;
	tetScore    = gameData.d.tetScore;
	memcpy(tetField, gameData.d.tetField, sizeof(tetField));

	// Guard against a corrupt/odd save so the game can never start broken.
	if ((tetType > 6) || (tetRot > 3) || (tetNextType > 6) || (tetLevel < 1))
	{
		gameData.d.tetSaved = 0;
		return false;
	}

	tetDropMs = 800 - ((tetLevel - 1) * 70);
	if (tetDropMs < 100)
	{
		tetDropMs = 100;
	}
	tetBagIndex = 7; // start a fresh bag
	tetNewRecord = false;
	return true;
}

menuStatus_t menuGameTetris(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		gamePaletteBuild();
		gameScoresLoad();
		gameRandSeed();

		if (tetRestoreState())
		{
			// Pick up exactly where the player left off, held in pause.
			tetState = GAME_STATE_PAUSED;
		}
		else
		{
			tetReset();
			tetState = GAME_STATE_START;
		}

		tetLastDrop = ticksGetMillis();
		tetDraw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			tetSaveState();
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		switch (tetState)
		{
			case GAME_STATE_START:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					tetState = GAME_STATE_RUNNING;
					tetLastDrop = ev->time;
					tetDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_RUNNING:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
				{
					tetState = GAME_STATE_PAUSED;
					tetSaveState();
					tetDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					tetState = GAME_STATE_EXIT_CONFIRM;
					tetSaveState();
					tetDraw();
					return MENU_STATUS_SUCCESS;
				}
				// Rotary (remapped to LEFT/RIGHT here) nudges the piece sideways.
				if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
				{
					if (tetFits(tetType, tetRot, (tetX - 1), tetY)) { tetX--; }
					tetDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
				{
					if (tetFits(tetType, tetRot, (tetX + 1), tetY)) { tetX++; }
					tetDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
				{
					switch (ev->keys.key)
					{
						case '4':
							if (tetFits(tetType, tetRot, (tetX - 1), tetY)) { tetX--; }
							break;

						case '6':
							if (tetFits(tetType, tetRot, (tetX + 1), tetY)) { tetX++; }
							break;

						case '2':
							tetRotate();
							break;

						case '8': // fast drop - straight to the bottom
						case '0': // hard drop - same thing, both land the piece
							{
								int dropped = 0;

								while (tetFits(tetType, tetRot, tetX, (tetY + 1)))
								{
									tetY++;
									dropped++;
								}
								if (tetScore < (0xFFFFFFFFU - 1000U))
								{
									tetScore += (uint32_t) dropped;
								}
								tetLock();
								tetLastDrop = ev->time;
							}
							break;

						default:
							break;
					}
					tetDraw();
					return MENU_STATUS_SUCCESS;
				}
				break;

			case GAME_STATE_PAUSED:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					tetState = GAME_STATE_RUNNING;
					tetLastDrop = ev->time;
					tetDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_GAME_OVER:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					tetReset();
					tetState = GAME_STATE_RUNNING;
					tetLastDrop = ev->time;
					tetDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_EXIT_CONFIRM:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					menuSystemPopPreviousMenu();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					tetState = GAME_STATE_RUNNING;
					tetLastDrop = ev->time;
					tetDraw();
				}
				return MENU_STATUS_SUCCESS;
		}
	}

	// Gravity - non-blocking, overflow-safe.
	if (tetState == GAME_STATE_RUNNING)
	{
		if ((uint32_t)(ev->time - tetLastDrop) >= (uint32_t) tetDropMs)
		{
			tetLastDrop = ev->time;

			if (tetFits(tetType, tetRot, tetX, (tetY + 1)))
			{
				tetY++;
			}
			else
			{
				tetLock();
			}
			tetDraw();
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Game 5: Dino runner
// Endless runner. Everything is drawn from rectangles - no bitmap assets - so it
// costs almost no flash and follows the active theme like the other games.
// ---------------------------------------------------------------------------
#define DINO_GROUND    104
#define DINO_X         18
#define DINO_FP        4      // 1/16 pixel fixed point
#define DINO_TICK_MS   33     // ~30 fps
#define DINO_MAX_OBS   4
#define DINO_RUN_W     13
#define DINO_RUN_H     18
#define DINO_DUCK_W    18
#define DINO_DUCK_H    11
// A short, snappy arc: 28px peak in 19 ticks (~0.6s), which stays clear of the
// 20px cactus for 10 of them. Anything floatier and the dino is still airborne
// long after the obstacle has gone past - it looks wrong and it lands you on
// the next one.
#define DINO_JUMP_V    (-100)
#define DINO_GRAVITY   10
#define DINO_AIR_TICKS 19

// One rule the player can actually learn: cactus means jump, bird means duck.
// (An earlier "high bird" that had to be ignored rather than reacted to just
// punished the obvious reaction, so it is gone.)
#define DINO_OBS_TYPES 3
static const uint8_t dinoObsW[DINO_OBS_TYPES]    = { 7, 10, 14 };
static const uint8_t dinoObsH[DINO_OBS_TYPES]    = { 14, 20, 9 };
// Base height above the ground: the bird sits just below the standing dino's
// head, and just above the ducking one's back.
static const uint8_t dinoObsYOff[DINO_OBS_TYPES] = { 0, 0, 14 };

static int       dinoY;   // fixed point offset from the ground (negative = airborne)
static int       dinoVY;
static bool      dinoDuck;
static int       dinoObsX[DINO_MAX_OBS];
static uint8_t   dinoObsType[DINO_MAX_OBS];
static bool      dinoObsOn[DINO_MAX_OBS];
static int       dinoSpeed;
static int       dinoSpawnGap;
static uint32_t  dinoDistance;
static uint32_t  dinoScore;
static int       dinoAnim;
static uint32_t  dinoLastTick = 0;
static bool      dinoNewRecord = false;
static GameState dinoState = GAME_STATE_START;

static void dinoReset(void)
{
	dinoY = 0;
	dinoVY = 0;
	dinoDuck = false;
	dinoSpeed = 48;   // 3 px per tick
	dinoSpawnGap = 90;
	dinoDistance = 0;
	dinoScore = 0;
	dinoAnim = 0;
	dinoNewRecord = false;

	for (int i = 0; i < DINO_MAX_OBS; i++)
	{
		dinoObsOn[i] = false;
	}
}

static void dinoSpawnObstacle(void)
{
	for (int i = 0; i < DINO_MAX_OBS; i++)
	{
		if (!dinoObsOn[i])
		{
			dinoObsOn[i] = true;
			dinoObsX[i] = ((DISPLAY_SIZE_X + 6) << DINO_FP);
			// Birds only appear once the pace has picked up.
			dinoObsType[i] = (uint8_t)((dinoSpeed > 60) ? (gameRand() % (uint32_t) DINO_OBS_TYPES) : (gameRand() % 2U));
			return;
		}
	}
}

static bool dinoHits(void)
{
	// Always the narrow width: ducking must never make you easier to hit.
	int dw = DINO_RUN_W;
	int dh = (dinoDuck ? DINO_DUCK_H : DINO_RUN_H);
	int dBottom = DINO_GROUND + (dinoY >> DINO_FP);
	int dTop = dBottom - dh;

	for (int i = 0; i < DINO_MAX_OBS; i++)
	{
		int ox;
		int ow;
		int oBottom;
		int oTop;

		if (!dinoObsOn[i])
		{
			continue;
		}

		ox = (dinoObsX[i] >> DINO_FP);
		ow = (int) dinoObsW[dinoObsType[i]];
		oBottom = DINO_GROUND - (int) dinoObsYOff[dinoObsType[i]];
		oTop = oBottom - (int) dinoObsH[dinoObsType[i]];

		// 1px of forgiveness so near-misses do not feel unfair
		if (((DINO_X + dw - 1) > (ox + 1)) && ((DINO_X + 1) < (ox + ow - 1)) &&
				((dBottom - 1) > oTop) && ((dTop + 1) < oBottom))
		{
			return true;
		}
	}
	return false;
}

static void dinoPhysics(void)
{
	if ((dinoY < 0) || (dinoVY < 0))
	{
		dinoVY += DINO_GRAVITY;
		dinoY += dinoVY;
		if (dinoY >= 0)
		{
			dinoY = 0;
			dinoVY = 0;
		}
	}

	for (int i = 0; i < DINO_MAX_OBS; i++)
	{
		if (dinoObsOn[i])
		{
			dinoObsX[i] -= dinoSpeed;
			if ((dinoObsX[i] >> DINO_FP) < -16) // fully off the left edge
			{
				dinoObsOn[i] = false;
			}
		}
	}

	dinoSpawnGap -= (dinoSpeed >> DINO_FP);
	if (dinoSpawnGap <= 0)
	{
		int pxPerTick = (dinoSpeed >> DINO_FP);

		dinoSpawnObstacle();
		// Leave a full jump arc plus half again between obstacles, scaled with
		// the current pace, so every one of them stays clearable.
		dinoSpawnGap = (((DINO_AIR_TICKS * pxPerTick) * 3) / 2) + (int)(gameRand() % 60U);
	}

	dinoDistance += (uint32_t)(dinoSpeed >> DINO_FP);
	dinoScore = (dinoDistance / 10U);

	if (((dinoDistance % 400U) < (uint32_t)(dinoSpeed >> DINO_FP)) && (dinoSpeed < 96))
	{
		dinoSpeed += 2;
	}

	dinoAnim++;

	if (dinoHits())
	{
		dinoState = GAME_STATE_GAME_OVER;
		dinoNewRecord = gameScoreSubmit(GAME_ID_DINO, dinoScore);
		gameBeep(dinoNewRecord ? MELODY_ACK_BEEP : MELODY_NACK_BEEP);
	}
}

static void dinoDrawFigure(void)
{
	int bottom = DINO_GROUND + (dinoY >> DINO_FP);
	int x = DINO_X;

	gameSetColour(gamePal.danger); // red, in whichever shade the theme allows

	if (dinoDuck)
	{
		int top = bottom - DINO_DUCK_H;

		// Kept inside DINO_RUN_W so the sprite matches the hit box.
		gameFillRectClipped(x, (top + 2), 9, 7);
		gameFillRectClipped((x + 8), top, 5, 6);
		gameFillRectClipped((x + 1), (bottom - 2), 3, 2);
		gameFillRectClipped((x + 6), (bottom - 2), 3, 2);
		gameSetColour(gamePal.background);
		gameFillRectClipped((x + 10), (top + 1), 1, 1);
	}
	else
	{
		int top = bottom - DINO_RUN_H;

		gameFillRectClipped(x, (top + 6), 9, 8);
		gameFillRectClipped((x + 6), top, 7, 7);
		gameFillRectClipped((x - 3), (top + 7), 3, 3);

		if (((dinoAnim / 4) & 1) != 0)
		{
			gameFillRectClipped((x + 1), (top + 14), 3, 4);
			gameFillRectClipped((x + 6), (top + 14), 3, 2);
		}
		else
		{
			gameFillRectClipped((x + 1), (top + 14), 3, 2);
			gameFillRectClipped((x + 6), (top + 14), 3, 4);
		}
		gameSetColour(gamePal.background);
		gameFillRectClipped((x + 10), (top + 2), 1, 1);
	}
}

static void dinoDraw(void)
{
	char buf[24];

	displayClearBuf();
	gameClearScreen();
	gameDrawHeader("DINO", dinoScore);

	// Ground plus scrolling dashes, which sell the speed
	gameSetColour(gamePal.border);
	displayFillRect(0, DINO_GROUND, DISPLAY_SIZE_X, 1, false);
	gameSetColour(gamePal.secondaryText);
	for (int i = 0; i < DISPLAY_SIZE_X; i += 18)
	{
		int x = (i - (int)(dinoDistance % 18U));

		if ((x >= 0) && (x < (DISPLAY_SIZE_X - 6)))
		{
			displayFillRect(x, (DINO_GROUND + 3), 6, 1, false);
		}
	}

	for (int i = 0; i < DINO_MAX_OBS; i++)
	{
		int x;
		int t;
		int w;
		int h;
		int bottom;
		int top;

		if (!dinoObsOn[i])
		{
			continue;
		}

		x = (dinoObsX[i] >> DINO_FP);
		t = (int) dinoObsType[i];
		w = (int) dinoObsW[t];
		h = (int) dinoObsH[t];
		bottom = DINO_GROUND - (int) dinoObsYOff[t];
		top = bottom - h;

		if ((x >= DISPLAY_SIZE_X) || ((x + w) < 0))
		{
			continue;
		}

		if (t <= 1) // cactus
		{
			gameSetColour(gamePal.success);
			gameFillRectClipped((x + (w / 2) - 1), top, 3, h);
			gameFillRectClipped(x, (top + (h / 3)), w, 2);
			gameFillRectClipped(x, (top + (h / 3)), 2, (h / 4));
			gameFillRectClipped((x + w - 2), (top + (h / 3)), 2, (h / 4));
		}
		else // bird - not red, so it never gets confused with the dino
		{
			gameSetColour(gamePal.gameObject2);
			gameFillRectClipped((x + 4), (top + 3), 8, 3);
			if (((dinoAnim / 5) & 1) != 0)
			{
				gameFillRectClipped(x, top, 5, 3);
			}
			else
			{
				gameFillRectClipped(x, (top + 6), 5, 3);
			}
		}
	}

	dinoDrawFigure();

	switch (dinoState)
	{
		case GAME_STATE_START:
			gameDrawPanel("DINO", GT("2 veya Yesil: Zipla", "2 or Green: Jump", "2 o Verde: Saltar", "2 \xE8\xEB\xE8 \xC7\xE5\xEB: \xCF\xF0\xFB\xE6\xEE\xEA", "2 oder Gr\xFCn: Sprung"), GT("8: Egil     * Duraklat", "8: Duck     * Pause", "8: Agachar  * Pausa", "8: \xCF\xF0\xE8\xF1\xE5\xF1\xF2\xFC * \xCF\xE0\xF3\xE7\xE0", "8: Ducken   * Pause"),
					GT("Yesil: Basla   Kirmizi: Geri", "Green: Start   Red: Back", "Verde: Jugar   Rojo: Atr\xE1s", "\xC7\xE5\xEB: \xD1\xF2\xE0\xF0\xF2 \xCA\xF0\xE0\xF1: \xCD\xE0\xE7\xE0\xE4", "Gr\xFCn: Start Rot: Zur\xFC""ck"), gamePal.accent);
			break;

		case GAME_STATE_PAUSED:
			gameDrawPanel(GT("DURAKLATILDI", "PAUSED", "PAUSA", "\xCF\xC0\xD3\xC7\xC0", "PAUSE"), NULL, GT("* veya Yesil: Devam", "* or Green: Resume", "* o Verde: Seguir", "* \xE8\xEB\xE8 \xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5", "* oder Gr\xFCn: Weiter"), GT("Kirmizi: Cikis", "Red: Exit", "Rojo: Salir", "\xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4", "Rot: Ende"), gamePal.warning);
			break;

		case GAME_STATE_GAME_OVER:
			{
				char b2[24];

				snprintf(buf, sizeof(buf), GT("Puan: %u", "Score: %u", "Puntos: %u", "\xCE\xF7\xEA\xE8: %u", "Punkte: %u"), (unsigned) dinoScore);
				snprintf(b2, sizeof(b2), GT("Rekor: %u", "Best: %u", "M\xE1x: %u", "\xD0\xE5\xEA\xEE\xF0\xE4: %u", "Best: %u"), (unsigned) gameScoreGet(GAME_ID_DINO));
				gameDrawPanel((dinoNewRecord ? GT("YENI REKOR", "NEW RECORD", "R\xC9""CORD!", "\xD0\xC5\xCA\xCE\xD0\xC4!", "REKORD!") : GT("OYUN BITTI", "GAME OVER", "FIN DEL JUEGO", "\xCA\xCE\xCD\xC5\xD6 \xC8\xC3\xD0\xDB", "GAME OVER")), buf, b2,
						GT("Yesil: Tekrar   Kirmizi: Cikis", "Green: Again   Red: Exit", "Verde: Otra  Rojo: Salir", "\xC7\xE5\xEB: \xC5\xF9\xB8  \xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4", "Gr\xFCn: Neu  Rot: Ende"), (dinoNewRecord ? gamePal.success : gamePal.danger));
			}
			break;

		case GAME_STATE_EXIT_CONFIRM:
			gameDrawPanel(GT("CIKIS", "EXIT", "SALIR", "\xC2\xDB\xD5\xCE\xC4", "ENDE"), GT("Oyundan cikilsin mi?", "Leave the game?", "Salir del juego?", "\xC2\xFB\xE9\xF2\xE8 \xE8\xE7 \xE8\xE3\xF0\xFB?", "Spiel verlassen?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2", "Gr\xFCn: Ja   Rot: Nein"), gamePal.warning);
			break;

		default:
			break;
	}

	displayRender();
}

static void dinoJump(void)
{
	if (dinoY == 0) // only from the ground
	{
		dinoVY = DINO_JUMP_V;
		dinoDuck = false;
		gameBeep(MELODY_KEY_BEEP);
	}
}

menuStatus_t menuGameDino(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		gamePaletteBuild();
		gameScoresLoad();
		gameRandSeed();
		dinoReset();
		dinoState = GAME_STATE_START;
		dinoLastTick = ticksGetMillis();
		dinoDraw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		switch (dinoState)
		{
			case GAME_STATE_START:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					dinoState = GAME_STATE_RUNNING;
					dinoLastTick = ev->time;
					dinoDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_RUNNING:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
				{
					dinoState = GAME_STATE_PAUSED;
					dinoDuck = false;
					dinoDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					dinoState = GAME_STATE_EXIT_CONFIRM;
					dinoDuck = false;
					dinoDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					dinoJump();
					return MENU_STATUS_SUCCESS;
				}
				// 2 jumps, 8 is held down to duck
				if (ev->keys.key == '2')
				{
					if ((ev->keys.event & KEY_MOD_UP) == 0)
					{
						dinoJump();
					}
					return MENU_STATUS_SUCCESS;
				}
				if (ev->keys.key == '8')
				{
					dinoDuck = ((ev->keys.event & KEY_MOD_UP) == 0);
					return MENU_STATUS_SUCCESS;
				}
				break;

			case GAME_STATE_PAUSED:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					dinoState = GAME_STATE_RUNNING;
					dinoLastTick = ev->time;
					dinoDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_GAME_OVER:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					dinoReset();
					dinoState = GAME_STATE_RUNNING;
					dinoLastTick = ev->time;
					dinoDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_EXIT_CONFIRM:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					menuSystemPopPreviousMenu();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					dinoState = GAME_STATE_RUNNING;
					dinoLastTick = ev->time;
					dinoDraw();
				}
				return MENU_STATUS_SUCCESS;
		}
	}

	if (dinoState == GAME_STATE_RUNNING)
	{
		if ((uint32_t)(ev->time - dinoLastTick) >= DINO_TICK_MS)
		{
			dinoLastTick = ev->time;
			dinoPhysics();
			dinoDraw();
		}
	}

	return MENU_STATUS_SUCCESS;
}
