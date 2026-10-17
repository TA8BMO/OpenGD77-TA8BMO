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
#define GT(tr, en, es, ru) LANG4((tr), (en), (es), (ru)) // Turkish, English, Spanish, Russian

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
	"3. Tugla Kirma",
	"4. 2048",
	"5. Dinozor",
	"6. Skorlar"
};
static const char * const gamesMenuItemsEN[] =
{
	"1. Snake",
	"2. Tetris",
	"3. Breakout",
	"4. 2048",
	"5. Dino",
	"6. Scores"
};
static const char * const gamesMenuItemsES[] =
{
	"1. Serpiente",
	"2. Tetris",
	"3. Ladrillos",
	"4. 2048",
	"5. Dino",
	"6. R\xE9""cords"
};
#define GAMES_MENU_COUNT  (sizeof(gamesMenuItems) / sizeof(gamesMenuItems[0]))
_Static_assert((sizeof(gamesMenuItemsES) == sizeof(gamesMenuItems)), "games menu: Turkish and Spanish lists differ in length");
static const char * const gamesMenuItemsRU[] =
{
	"1. \xC7\xEC\xE5\xE9\xEA\xE0",
	"2. \xD2\xE5\xF2\xF0\xE8\xF1",
	"3. \xC0\xF0\xEA\xE0\xED\xEE\xE8\xE4",
	"4. 2048",
	"5. \xC4\xE8\xED\xEE",
	"6. \xD0\xE5\xEA\xEE\xF0\xE4\xFB"
};
_Static_assert((sizeof(gamesMenuItemsRU) == sizeof(gamesMenuItems)), "games menu: Turkish and Russian lists differ in length");
_Static_assert((sizeof(gamesMenuItemsEN) == sizeof(gamesMenuItems)), "games menu: Turkish and English lists differ in length");

static const int gamesMenuTargets[GAMES_MENU_COUNT] =
{
	UI_GAME_SNAKE, UI_GAME_TETRIS, UI_GAME_BREAKOUT, UI_GAME_2048, UI_GAME_DINO, UI_GAME_SCORES
};

static int gamesMenuIndex = 0;

static void gamesMenuDraw(void)
{
	gamePaletteBuild();
	displayClearBuf();
	gameClearScreen();

	gameSetColour(gamePal.accent);
	displayPrintCentered(3, GT("OYUNLAR", "GAMES", "JUEGOS", "\xC8\xC3\xD0\xDB"), FONT_SIZE_3);
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
		displayPrintCore(10, y, GT(gamesMenuItems[i], gamesMenuItemsEN[i], gamesMenuItemsES[i], gamesMenuItemsRU[i]), FONT_SIZE_3, TEXT_ALIGN_LEFT, focused);
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

		// Direct selection with 1..5
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
// Score screen (menu item 5)
// ---------------------------------------------------------------------------
#define GAME_SCORE_ROWS 5
static const char * const gameScoreNames[GAME_SCORE_ROWS] = { "Yilan", "Tetris", "Tugla", "2048", "Dino" };
static const char * const gameScoreNamesEN[GAME_SCORE_ROWS] = { "Snake", "Tetris", "Breakout", "2048", "Dino" };
static const char * const gameScoreNamesES[GAME_SCORE_ROWS] = { "Serpiente", "Tetris", "Ladrillos", "2048", "Dino" };
static const char * const gameScoreNamesRU[GAME_SCORE_ROWS] = { "\xC7\xEC\xE5\xE9\xEA\xE0", "\xD2\xE5\xF2\xF0\xE8\xF1", "\xC0\xF0\xEA\xE0\xED\xEE\xE8\xE4", "2048", "\xC4\xE8\xED\xEE" };
static const uint8_t gameScoreIds[GAME_SCORE_ROWS] =
		{ GAME_ID_SNAKE, GAME_ID_TETRIS, GAME_ID_BREAKOUT, GAME_ID_2048, GAME_ID_DINO };
static bool gameScoresConfirmReset = false;

static void gameScoresDraw(void)
{
	char buf[24];

	gamePaletteBuild();
	displayClearBuf();
	gameClearScreen();

	if (gameScoresConfirmReset)
	{
		gameDrawPanel(GT("SKORLAR", "SCORES", "R\xC9""CORDS", "\xD0\xC5\xCA\xCE\xD0\xC4\xDB"), GT("Tum skorlar silinsin mi?", "Delete all scores?", "Borrar los r\xE9""cords?", "\xD3\xE4\xE0\xEB\xE8\xF2\xFC \xF0\xE5\xEA\xEE\xF0\xE4\xFB?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2"), gamePal.danger);
		displayRender();
		return;
	}

	gameSetColour(gamePal.accent);
	displayPrintCentered(4, GT("EN YUKSEK SKORLAR", "HIGH SCORES", "MEJORES R\xC9""CORDS", "\xD0\xC5\xCA\xCE\xD0\xC4\xDB"), FONT_SIZE_1);
	gameSetColour(gamePal.border);
	displayFillRect(4, 18, (DISPLAY_SIZE_X - 8), 1, false);

	for (int i = 0; i < GAME_SCORE_ROWS; i++)
	{
		int y = 26 + (i * 17);

		gameSetColour(gamePal.foreground);
		displayPrintAt(8, y, GT(gameScoreNames[i], gameScoreNamesEN[i], gameScoreNamesES[i], gameScoreNamesRU[i]), FONT_SIZE_3);

		snprintf(buf, sizeof(buf), "%u", (unsigned) gameScoreGet(gameScoreIds[i]));
		gameSetColour(gamePal.gameObject3);
		displayPrintAt((DISPLAY_SIZE_X - (int)(strlen(buf) * 8) - 8), y, buf, FONT_SIZE_3);
	}

	gameSetColour(gamePal.secondaryText);
	displayPrintCentered((DISPLAY_SIZE_Y - 12), GT("SK2+# uzun: sifirla", "SK2+# long: reset", "SK2+# largo: borrar", "SK2+# \xE4\xEE\xEB\xE3\xEE: \xF1\xE1\xF0\xEE\xF1"), FONT_SIZE_1);

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
	gameDrawHeader(GT("YILAN", "SNAKE", "SERPIENTE", "\xC7\xCC\xC5\xC9\xCA\xC0"), snakeScore);

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
			gameDrawPanel(GT("YILAN", "SNAKE", "SERPIENTE", "\xC7\xCC\xC5\xC9\xCA\xC0"), GT("2 Yukari  8 Asagi", "2 Up  8 Down", "2 Arriba  8 Abajo", "2 \xC2\xE2\xE5\xF0\xF5  8 \xC2\xED\xE8\xE7"), GT("4 Sol  6 Sag   * Duraklat", "4 Left  6 Right  * Pause", "4 Izq.  6 Der.  * Pausa", "4 \xCB\xE5\xE2\xEE  6 \xCF\xF0\xE0\xE2\xEE  * \xCF\xE0\xF3\xE7\xE0"),
					GT("Yesil: Basla    Kirmizi: Geri", "Green: Start   Red: Back", "Verde: Jugar   Rojo: Atr\xE1s", "\xC7\xE5\xEB: \xD1\xF2\xE0\xF0\xF2 \xCA\xF0\xE0\xF1: \xCD\xE0\xE7\xE0\xE4"), gamePal.accent);
			break;

		case GAME_STATE_PAUSED:
			gameDrawPanel(GT("DURAKLATILDI", "PAUSED", "PAUSA", "\xCF\xC0\xD3\xC7\xC0"), NULL, GT("* veya Yesil: Devam", "* or Green: Resume", "* o Verde: Seguir", "* \xE8\xEB\xE8 \xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5"), GT("Kirmizi: Cikis", "Red: Exit", "Rojo: Salir", "\xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), gamePal.warning);
			break;

		case GAME_STATE_GAME_OVER:
			snprintf(buf, sizeof(buf), GT("Puan: %u", "Score: %u", "Puntos: %u", "\xCE\xF7\xEA\xE8: %u"), (unsigned) snakeScore);
			{
				char buf2[24];
				snprintf(buf2, sizeof(buf2), GT("Rekor: %u", "Best: %u", "M\xE1x: %u", "\xD0\xE5\xEA\xEE\xF0\xE4: %u"), (unsigned) gameScoreGet(GAME_ID_SNAKE));
				gameDrawPanel((snakeNewRecord ? GT("YENI REKOR", "NEW RECORD", "R\xC9""CORD!", "\xD0\xC5\xCA\xCE\xD0\xC4!") : GT("OYUN BITTI", "GAME OVER", "FIN DEL JUEGO", "\xCA\xCE\xCD\xC5\xD6 \xC8\xC3\xD0\xDB")), buf, buf2,
						GT("Yesil: Tekrar   Kirmizi: Cikis", "Green: Again   Red: Exit", "Verde: Otra  Rojo: Salir", "\xC7\xE5\xEB: \xC5\xF9\xB8  \xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), (snakeNewRecord ? gamePal.success : gamePal.danger));
			}
			break;

		case GAME_STATE_EXIT_CONFIRM:
			gameDrawPanel(GT("CIKIS", "EXIT", "SALIR", "\xC2\xDB\xD5\xCE\xC4"), GT("Oyundan cikilsin mi?", "Leave the game?", "Salir del juego?", "\xC2\xFB\xE9\xF2\xE8 \xE8\xE7 \xE8\xE3\xF0\xFB?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2"), gamePal.warning);
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
	displayPrintAt(TET_PANEL_X, 18, GT("Puan", "Score", "Ptos.", "\xCE\xF7\xEA\xE8"), FONT_SIZE_1);
	displayPrintAt(TET_PANEL_X, 40, GT("Seviye", "Level", "Nivel", "\xD3\xF0."), FONT_SIZE_1);
	displayPrintAt(TET_PANEL_X, 62, GT("Satir", "Lines", "Filas", "\xD0\xFF\xE4\xFB"), FONT_SIZE_1);
	displayPrintAt(TET_PANEL_X, 84, GT("Sonraki", "Next", "Pr\xF3xima", "\xC4\xE0\xEB\xE5\xE5"), FONT_SIZE_1);

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
			gameDrawPanel("TETRIS", GT("4/6 Sol-Sag  2 Dondur", "4/6 Move  2 Rotate", "4/6 Mover  2 Girar", "4/6 \xD1\xE4\xE2\xE8\xE3  2 \xCF\xEE\xE2\xEE\xF0\xEE\xF2"), GT("8 Indir  0 Birak  * Dur", "8/0 Drop     * Pause", "8/0 Soltar   * Pausa", "8/0 \xD1\xE1\xF0\xEE\xF1   * \xCF\xE0\xF3\xE7\xE0"),
					GT("Yesil: Basla   Kirmizi: Geri", "Green: Start   Red: Back", "Verde: Jugar   Rojo: Atr\xE1s", "\xC7\xE5\xEB: \xD1\xF2\xE0\xF0\xF2 \xCA\xF0\xE0\xF1: \xCD\xE0\xE7\xE0\xE4"), gamePal.accent);
			break;

		case GAME_STATE_PAUSED:
			gameDrawPanel(GT("DURAKLATILDI", "PAUSED", "PAUSA", "\xCF\xC0\xD3\xC7\xC0"), NULL, GT("* veya Yesil: Devam", "* or Green: Resume", "* o Verde: Seguir", "* \xE8\xEB\xE8 \xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5"), GT("Kirmizi: Cikis", "Red: Exit", "Rojo: Salir", "\xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), gamePal.warning);
			break;

		case GAME_STATE_GAME_OVER:
			{
				char b2[20];

				snprintf(buf, sizeof(buf), GT("Puan: %u", "Score: %u", "Puntos: %u", "\xCE\xF7\xEA\xE8: %u"), (unsigned) tetScore);
				snprintf(b2, sizeof(b2), GT("Rekor: %u", "Best: %u", "M\xE1x: %u", "\xD0\xE5\xEA\xEE\xF0\xE4: %u"), (unsigned) gameScoreGet(GAME_ID_TETRIS));
				gameDrawPanel((tetNewRecord ? GT("YENI REKOR", "NEW RECORD", "R\xC9""CORD!", "\xD0\xC5\xCA\xCE\xD0\xC4!") : GT("OYUN BITTI", "GAME OVER", "FIN DEL JUEGO", "\xCA\xCE\xCD\xC5\xD6 \xC8\xC3\xD0\xDB")), buf, b2,
						GT("Yesil: Tekrar   Kirmizi: Cikis", "Green: Again   Red: Exit", "Verde: Otra  Rojo: Salir", "\xC7\xE5\xEB: \xC5\xF9\xB8  \xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), (tetNewRecord ? gamePal.success : gamePal.danger));
			}
			break;

		case GAME_STATE_EXIT_CONFIRM:
			gameDrawPanel(GT("CIKIS", "EXIT", "SALIR", "\xC2\xDB\xD5\xCE\xC4"), GT("Oyundan cikilsin mi?", "Leave the game?", "Salir del juego?", "\xC2\xFB\xE9\xF2\xE8 \xE8\xE7 \xE8\xE3\xF0\xFB?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2"), gamePal.warning);
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
// Game 3: Breakout / Tugla Kirma
// The ball moves in 1/16-pixel fixed point and is stepped in small sub-steps,
// so it can never tunnel through a brick even at high speed.
// ---------------------------------------------------------------------------
#define BRK_COLS       8
#define BRK_ROWS       5
#define BRK_BW         19
#define BRK_BH         7
#define BRK_BX0        3
#define BRK_BY0        18
#define BRK_LEFT       2
#define BRK_RIGHT      (DISPLAY_SIZE_X - 3)
#define BRK_TOP        15
#define BRK_BOTTOM     (DISPLAY_SIZE_Y - 2)
#define BRK_PAD_Y      116
#define BRK_PAD_H      4
#define BRK_PAD_W      26
#define BRK_PAD_W_LONG 42
#define BRK_FP         4      // fixed point shift (1/16 px)
#define BRK_STEP_MS    25     // physics tick
#define BRK_BONUS_MS   10000  // how long a caught bonus lasts

enum { BRK_EMPTY = 0, BRK_NORMAL, BRK_BONUS, BRK_STRONG2, BRK_STRONG1 };
enum { BRK_BONUS_LONGPAD = 0, BRK_BONUS_SLOWBALL };

__attribute__((section(".ccmram"))) static uint8_t brkBricks[BRK_ROWS][BRK_COLS];

static int       brkPadX;
static int       brkPadW = BRK_PAD_W;
static int       brkPadDir = 0;   // -1/0/+1 while a direction key is held
#define BRK_SPEED_LEVELS 3
static int       brkSpeedLevel = 1; // 1..3, set with UP/DOWN; 1 = the original pace
// Paddle keeps up with the ball: same step at 1-2, +25% at the fastest setting.
static const uint8_t brkPadStepTable[BRK_SPEED_LEVELS] = { 4, 4, 5 };
static int       brkBallX, brkBallY, brkBallVX, brkBallVY; // fixed point
static int       brkSpeed;
static bool      brkBallOnPad = true;
static int       brkLives = 3;
static int       brkLevel = 1;
static uint32_t  brkScore = 0;
static GameState brkState = GAME_STATE_START;
static uint32_t  brkLastUpdate = 0;
static bool      brkNewRecord = false;
static bool      brkBonusFalling = false;
static int       brkBonusX, brkBonusY, brkBonusType;
static uint32_t  brkLongUntil = 0;
static uint32_t  brkSlowUntil = 0;

static void brkResetBall(void)
{
	brkBallOnPad = true;
	brkBallX = ((brkPadX + (brkPadW / 2)) << BRK_FP);
	brkBallY = ((BRK_PAD_Y - 3) << BRK_FP);
	brkBallVX = 0;
	brkBallVY = 0;
}

// Ball speed = level pace + the operator's own setting (UP/DOWN keys).
// Changing it mid-rally rescales the live velocity so it is felt immediately.
static void brkApplySpeed(void)
{
	int old = brkSpeed;

	brkSpeed = (18 + ((brkLevel - 1) * 3)) + ((brkSpeedLevel - 1) * 6);
	if (brkSpeed > 60)
	{
		brkSpeed = 60;
	}

	if ((old > 0) && (brkBallOnPad == false))
	{
		brkBallVX = ((brkBallVX * brkSpeed) / old);
		brkBallVY = ((brkBallVY * brkSpeed) / old);
	}
}

static void brkResetLevel(void)
{
	for (int r = 0; r < BRK_ROWS; r++)
	{
		for (int c = 0; c < BRK_COLS; c++)
		{
			uint8_t t = BRK_NORMAL;

			if (r == 0)
			{
				t = BRK_STRONG2;
			}
			else if (((r + c + brkLevel) % 7) == 0)
			{
				t = BRK_BONUS;
			}
			else if (((r + c) % 5) == 0)
			{
				t = BRK_STRONG2;
			}
			brkBricks[r][c] = t;
		}
	}

	brkApplySpeed();
}

static void brkReset(void)
{
	brkLives = 3;
	brkLevel = 1;
	brkScore = 0;
	brkPadW = BRK_PAD_W;
	brkPadX = ((DISPLAY_SIZE_X - brkPadW) / 2);
	brkPadDir = 0;
	brkBonusFalling = false;
	brkLongUntil = 0;
	brkSlowUntil = 0;
	brkNewRecord = false;
	brkResetLevel();
	brkResetBall();
}

static bool brkBricksLeft(void)
{
	for (int r = 0; r < BRK_ROWS; r++)
	{
		for (int c = 0; c < BRK_COLS; c++)
		{
			if (brkBricks[r][c] != BRK_EMPTY)
			{
				return true;
			}
		}
	}
	return false;
}

static void brkLaunch(void)
{
	brkBallOnPad = false;
	brkBallVX = ((gameRand() & 1U) ? (brkSpeed / 2) : -(brkSpeed / 2));
	brkBallVY = -brkSpeed;
}

static void brkLoseLife(void)
{
	brkLives--;
	brkBonusFalling = false;
	brkLongUntil = 0;
	brkSlowUntil = 0;
	brkPadW = BRK_PAD_W;

	if (brkLives <= 0)
	{
		brkState = GAME_STATE_GAME_OVER;
		brkNewRecord = gameScoreSubmit(GAME_ID_BREAKOUT, brkScore);
		gameBeep(brkNewRecord ? MELODY_ACK_BEEP : MELODY_NACK_BEEP);
	}
	else
	{
		brkResetBall();
		gameBeep(MELODY_NACK_BEEP);
	}
}

static void brkAddScore(uint32_t pts)
{
	if (brkScore < (0xFFFFFFFFU - 100000U))
	{
		brkScore += pts;
	}
}

// Handle one brick hit at pixel (px,py); returns true when a brick was struck.
static bool brkHitBrick(int px, int py, bool cameFromSide)
{
	int c = (px - BRK_BX0) / BRK_BW;
	int r = (py - BRK_BY0) / BRK_BH;

	if ((c < 0) || (c >= BRK_COLS) || (r < 0) || (r >= BRK_ROWS))
	{
		return false;
	}
	if (brkBricks[r][c] == BRK_EMPTY)
	{
		return false;
	}

	switch (brkBricks[r][c])
	{
		case BRK_STRONG2:
			brkBricks[r][c] = BRK_STRONG1; // needs another hit
			brkAddScore(5);
			break;

		case BRK_STRONG1:
			brkBricks[r][c] = BRK_EMPTY;
			brkAddScore(20);
			break;

		case BRK_BONUS:
			brkBricks[r][c] = BRK_EMPTY;
			brkAddScore(10);
			if (!brkBonusFalling) // never let bonuses pile up
			{
				brkBonusFalling = true;
				brkBonusX = (BRK_BX0 + (c * BRK_BW) + (BRK_BW / 2));
				brkBonusY = (BRK_BY0 + (r * BRK_BH) + (BRK_BH / 2));
				brkBonusType = (int)(gameRand() & 1U);
			}
			break;

		default:
			brkBricks[r][c] = BRK_EMPTY;
			brkAddScore(10);
			break;
	}

	if (cameFromSide)
	{
		brkBallVX = -brkBallVX;
	}
	else
	{
		brkBallVY = -brkBallVY;
	}

	gameBeep(MELODY_KEY_BEEP);
	return true;
}

static void brkMovePaddle(int delta);

static void brkPhysics(uint32_t now)
{
	int steps;
	int sx, sy;

	// Holding 4 or 6 slides the paddle - the key handler only sets the direction.
	if (brkPadDir != 0)
	{
		brkMovePaddle(brkPadDir * (int) brkPadStepTable[brkSpeedLevel - 1]);
	}

	// Timed effects expire on their own.
	if ((brkLongUntil != 0) && ((int32_t)(now - brkLongUntil) >= 0))
	{
		brkLongUntil = 0;
		brkPadW = BRK_PAD_W;
	}
	if ((brkSlowUntil != 0) && ((int32_t)(now - brkSlowUntil) >= 0))
	{
		brkSlowUntil = 0;
	}

	// Falling bonus
	if (brkBonusFalling)
	{
		brkBonusY += 2;

		if ((brkBonusY >= BRK_PAD_Y) && (brkBonusY <= (BRK_PAD_Y + BRK_PAD_H + 2)) &&
				(brkBonusX >= brkPadX) && (brkBonusX <= (brkPadX + brkPadW)))
		{
			brkBonusFalling = false;
			brkAddScore(25);
			if (brkBonusType == BRK_BONUS_LONGPAD)
			{
				brkPadW = BRK_PAD_W_LONG;
				brkLongUntil = now + BRK_BONUS_MS;
			}
			else
			{
				brkSlowUntil = now + BRK_BONUS_MS;
			}
			gameBeep(MELODY_ACK_BEEP);
		}
		else if (brkBonusY > BRK_BOTTOM)
		{
			brkBonusFalling = false;
		}
	}

	if (brkBallOnPad)
	{
		brkBallX = ((brkPadX + (brkPadW / 2)) << BRK_FP);
		brkBallY = ((BRK_PAD_Y - 3) << BRK_FP);
		return;
	}

	{
		int vx = brkBallVX;
		int vy = brkBallVY;

		if (brkSlowUntil != 0) // slow-ball bonus
		{
			vx = ((vx * 2) / 3);
			vy = ((vy * 2) / 3);
		}

		// Sub-step so a fast ball still tests every pixel it passes through.
		steps = 1;
		{
			int m = ((vx < 0) ? -vx : vx);
			int n = ((vy < 0) ? -vy : vy);

			if (n > m)
			{
				m = n;
			}
			steps = ((m >> BRK_FP) + 1) * 2;
			if (steps > 16)
			{
				steps = 16;
			}
		}
		// A sub-step must never round down to zero, otherwise the ball can sit on
		// a wall flipping its velocity every step and never leave it.
		sx = (vx / steps);
		sy = (vy / steps);
		if ((sx == 0) && (vx != 0))
		{
			sx = ((vx > 0) ? 1 : -1);
		}
		if ((sy == 0) && (vy != 0))
		{
			sy = ((vy > 0) ? 1 : -1);
		}

		for (int s = 0; s < steps; s++)
		{
			int oldPx = (brkBallX >> BRK_FP);
			int oldPy = (brkBallY >> BRK_FP);
			int px;
			int py;

			brkBallX += sx;
			brkBallY += sy;
			px = (brkBallX >> BRK_FP);
			py = (brkBallY >> BRK_FP);

			// Walls - park the ball just INSIDE the wall, never exactly on it, so
			// the same test cannot fire again on the following sub-step.
			if (px <= BRK_LEFT)
			{
				brkBallX = ((BRK_LEFT + 1) << BRK_FP);
				brkBallVX = ((brkBallVX < 0) ? -brkBallVX : brkBallVX);
				sx = ((sx < 0) ? -sx : sx);
			}
			else if (px >= BRK_RIGHT)
			{
				brkBallX = ((BRK_RIGHT - 1) << BRK_FP);
				brkBallVX = ((brkBallVX > 0) ? -brkBallVX : brkBallVX);
				sx = ((sx > 0) ? -sx : sx);
			}

			if (py <= BRK_TOP)
			{
				brkBallY = ((BRK_TOP + 1) << BRK_FP);
				brkBallVY = ((brkBallVY < 0) ? -brkBallVY : brkBallVY);
				sy = ((sy < 0) ? -sy : sy);
			}

			// Paddle - bounce angle depends on where it lands
			if ((brkBallVY > 0) && (py >= (BRK_PAD_Y - 2)) && (py <= (BRK_PAD_Y + BRK_PAD_H)) &&
					(px >= brkPadX) && (px <= (brkPadX + brkPadW)))
			{
				int rel = (px - (brkPadX + (brkPadW / 2)));      // -w/2 .. +w/2
				int nvx = ((rel * brkSpeed) / (brkPadW / 2));

				if (nvx > brkSpeed) { nvx = brkSpeed; }
				if (nvx < -brkSpeed) { nvx = -brkSpeed; }

				// Never leave the ball travelling straight up - that makes it
				// ping between the top wall and the paddle forever.
				if ((nvx < (brkSpeed / 4)) && (nvx > -(brkSpeed / 4)))
				{
					nvx = ((rel < 0) ? -(brkSpeed / 4) : (brkSpeed / 4));
				}

				brkBallVX = nvx;
				brkBallVY = -brkSpeed;
				brkBallY = ((BRK_PAD_Y - 3) << BRK_FP);
				sx = (brkBallVX / steps);
				sy = (brkBallVY / steps);
				gameBeep(MELODY_KEY_BEEP);
				continue;
			}

			// Bricks - the entry axis decides which way it bounces
			if ((py >= BRK_BY0) && (py < (BRK_BY0 + (BRK_ROWS * BRK_BH))) &&
					(px >= BRK_BX0) && (px < (BRK_BX0 + (BRK_COLS * BRK_BW))))
			{
				bool fromSide = (((oldPx - BRK_BX0) / BRK_BW) != ((px - BRK_BX0) / BRK_BW)) &&
						(((oldPy - BRK_BY0) / BRK_BH) == ((py - BRK_BY0) / BRK_BH));

				if (brkHitBrick(px, py, fromSide))
				{
					sx = (brkBallVX / steps);
					sy = (brkBallVY / steps);

					if (!brkBricksLeft())
					{
						brkLevel++;
						brkAddScore(100);
						brkResetLevel();
						brkResetBall();
						gameBeep(MELODY_ACK_BEEP);
						return;
					}
				}
			}

			// Dropped below the paddle
			if (py > BRK_BOTTOM)
			{
				brkLoseLife();
				return;
			}
		}
	}
}

static void brkDraw(void)
{
	char buf[24];

	displayClearBuf();
	gameClearScreen();
	gameDrawHeader(GT("TUGLA KIRMA", "BREAKOUT", "LADRILLOS", "\xC0\xD0\xCA\xC0\xCD\xCE\xC8\xC4"), brkScore);

	// Lives as small blocks
	gameSetColour(gamePal.success);
	for (int i = 0; i < brkLives; i++)
	{
		displayFillRect((DISPLAY_SIZE_X - 40 + (i * 6)), 4, 4, 4, false);
	}

	// Bricks
	for (int r = 0; r < BRK_ROWS; r++)
	{
		for (int c = 0; c < BRK_COLS; c++)
		{
			int x = BRK_BX0 + (c * BRK_BW);
			int y = BRK_BY0 + (r * BRK_BH);
			uint16_t col;

			if (brkBricks[r][c] == BRK_EMPTY)
			{
				continue;
			}

			switch (brkBricks[r][c])
			{
				case BRK_STRONG2: col = gamePal.danger;      break;
				case BRK_STRONG1: col = gamePal.warning;     break;
				case BRK_BONUS:   col = gamePal.gameObject3; break;
				default:          col = gamePal.gameObject1; break;
			}

			gameSetColour(col);
			displayFillRect(x, y, (BRK_BW - 1), (BRK_BH - 1), false);

			// Shape cues so the types read without relying on colour
			if ((brkBricks[r][c] == BRK_STRONG2) || (brkBricks[r][c] == BRK_STRONG1))
			{
				gameSetColour(gamePal.background);
				displayFillRect((x + 2), (y + 2), (BRK_BW - 5), (BRK_BH - 5), false);
			}
			else if (brkBricks[r][c] == BRK_BONUS)
			{
				gameSetColour(gamePal.background);
				displayFillRect((x + (BRK_BW / 2) - 1), (y + 2), 2, 2, false);
			}
		}
	}

	// Falling bonus
	if (brkBonusFalling)
	{
		gameSetColour(gamePal.gameObject2);
		displayFillRect((brkBonusX - 2), (brkBonusY - 2), 5, 5, false);
		gameSetColour(gamePal.background);
		displayFillRect((brkBonusX - 1), brkBonusY, 3, 1, false);
		if (brkBonusType == BRK_BONUS_SLOWBALL)
		{
			displayFillRect(brkBonusX, (brkBonusY - 1), 1, 3, false);
		}
	}

	// Paddle
	gameSetColour(gamePal.accent);
	displayFillRect(brkPadX, BRK_PAD_Y, brkPadW, BRK_PAD_H, false);
	gameSetColour(gamePal.foreground);
	displayFillRect(brkPadX, BRK_PAD_Y, brkPadW, 1, false);

	// Ball - drawn with a contrasting outline so it shows on any theme
	{
		int bx = (brkBallX >> BRK_FP);
		int by = (brkBallY >> BRK_FP);

		gameSetColour(gamePal.foreground);
		displayFillRect((bx - 2), (by - 2), 5, 5, false);
		gameSetColour(gamePal.gameObject2);
		displayFillRect((bx - 1), (by - 1), 3, 3, false);
	}

	// Level / bonus indicator
	gameSetColour(gamePal.secondaryText);
	snprintf(buf, sizeof(buf), GT("Sv%d Hiz%d%s%s", "Lv%d Spd%d%s%s", "Nv%d Vel%d%s%s", "\xD3\xF0%d \xD1\xEA%d%s%s"), brkLevel, brkSpeedLevel,
			((brkLongUntil != 0) ? GT(" U", " L", " G", " \xC4") : ""), ((brkSlowUntil != 0) ? GT(" Y", " S", " L", " \xCC") : ""));
	displayPrintAt(2, (DISPLAY_SIZE_Y - 9), buf, FONT_SIZE_1);

	switch (brkState)
	{
		case GAME_STATE_START:
			gameDrawPanel(GT("TUGLA KIRMA", "BREAKOUT", "LADRILLOS", "\xC0\xD0\xCA\xC0\xCD\xCE\xC8\xC4"), GT("4/6 Sol-Sag   Ok: Hiz", "4/6 Move   Arrows: Speed", "4/6 Mover  Flechas: Vel.", "4/6 \xD1\xE4\xE2\xE8\xE3  \xD1\xF2\xF0\xE5\xEB\xEA\xE8: \xD1\xEA."), GT("Yesil: Topu birak  * Dur", "Green: Launch  * Pause", "Verde: Lanzar  * Pausa", "\xC7\xE5\xEB: \xCF\xF3\xF1\xEA  * \xCF\xE0\xF3\xE7\xE0"),
					GT("Kirmizi: Geri", "Red: Back", "Rojo: Atr\xE1s", "\xCA\xF0\xE0\xF1: \xCD\xE0\xE7\xE0\xE4"), gamePal.accent);
			break;

		case GAME_STATE_PAUSED:
			gameDrawPanel(GT("DURAKLATILDI", "PAUSED", "PAUSA", "\xCF\xC0\xD3\xC7\xC0"), NULL, GT("* veya Yesil: Devam", "* or Green: Resume", "* o Verde: Seguir", "* \xE8\xEB\xE8 \xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5"), GT("Kirmizi: Cikis", "Red: Exit", "Rojo: Salir", "\xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), gamePal.warning);
			break;

		case GAME_STATE_GAME_OVER:
			{
				char b2[24];

				snprintf(buf, sizeof(buf), GT("Puan: %u", "Score: %u", "Puntos: %u", "\xCE\xF7\xEA\xE8: %u"), (unsigned) brkScore);
				snprintf(b2, sizeof(b2), GT("Rekor: %u", "Best: %u", "M\xE1x: %u", "\xD0\xE5\xEA\xEE\xF0\xE4: %u"), (unsigned) gameScoreGet(GAME_ID_BREAKOUT));
				gameDrawPanel((brkNewRecord ? GT("YENI REKOR", "NEW RECORD", "R\xC9""CORD!", "\xD0\xC5\xCA\xCE\xD0\xC4!") : GT("OYUN BITTI", "GAME OVER", "FIN DEL JUEGO", "\xCA\xCE\xCD\xC5\xD6 \xC8\xC3\xD0\xDB")), buf, b2,
						GT("Yesil: Tekrar   Kirmizi: Cikis", "Green: Again   Red: Exit", "Verde: Otra  Rojo: Salir", "\xC7\xE5\xEB: \xC5\xF9\xB8  \xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), (brkNewRecord ? gamePal.success : gamePal.danger));
			}
			break;

		case GAME_STATE_EXIT_CONFIRM:
			gameDrawPanel(GT("CIKIS", "EXIT", "SALIR", "\xC2\xDB\xD5\xCE\xC4"), GT("Oyundan cikilsin mi?", "Leave the game?", "Salir del juego?", "\xC2\xFB\xE9\xF2\xE8 \xE8\xE7 \xE8\xE3\xF0\xFB?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2"), gamePal.warning);
			break;

		default:
			break;
	}

	displayRender();
}

static void brkMovePaddle(int delta)
{
	brkPadX += delta;
	if (brkPadX < BRK_LEFT)
	{
		brkPadX = BRK_LEFT;
	}
	if ((brkPadX + brkPadW) > BRK_RIGHT)
	{
		brkPadX = (BRK_RIGHT - brkPadW);
	}
}

menuStatus_t menuGameBreakout(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		gamePaletteBuild();
		gameScoresLoad();
		gameRandSeed();
		brkReset();
		brkState = GAME_STATE_START;
		brkLastUpdate = ticksGetMillis();
		brkDraw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		// UP / DOWN set the ball speed, in any state.
		if (KEYCHECK_SHORTUP(ev->keys, KEY_UP))
		{
			if (brkSpeedLevel < BRK_SPEED_LEVELS)
			{
				brkSpeedLevel++;
				brkApplySpeed();
				gameBeep(MELODY_KEY_BEEP);
			}
			brkDraw();
			return MENU_STATUS_SUCCESS;
		}
		if (KEYCHECK_SHORTUP(ev->keys, KEY_DOWN))
		{
			if (brkSpeedLevel > 1)
			{
				brkSpeedLevel--;
				brkApplySpeed();
				gameBeep(MELODY_KEY_BEEP);
			}
			brkDraw();
			return MENU_STATUS_SUCCESS;
		}

		switch (brkState)
		{
			case GAME_STATE_START:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					brkState = GAME_STATE_RUNNING;
					brkLastUpdate = ev->time;
					brkDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_RUNNING:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
				{
					brkState = GAME_STATE_PAUSED;
					brkPadDir = 0;
					brkDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					brkState = GAME_STATE_EXIT_CONFIRM;
					brkPadDir = 0;
					brkDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					if (brkBallOnPad)
					{
						brkLaunch();
					}
					return MENU_STATUS_SUCCESS;
				}
				// Rotary gives fine paddle control (remapped to LEFT/RIGHT in menus)
				if (KEYCHECK_SHORTUP(ev->keys, KEY_LEFT))
				{
					brkMovePaddle(-4);
					brkDraw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_RIGHT))
				{
					brkMovePaddle(4);
					brkDraw();
					return MENU_STATUS_SUCCESS;
				}
				// Hold 4 / 6 to slide. Only the direction is latched here; the
				// physics tick moves the paddle, so it glides smoothly.
				if ((ev->keys.key == '4') || (ev->keys.key == '6'))
				{
					if (ev->keys.event & KEY_MOD_UP)
					{
						brkPadDir = 0;
					}
					else
					{
						brkPadDir = ((ev->keys.key == '4') ? -1 : 1);
					}
					return MENU_STATUS_SUCCESS;
				}
				break;

			case GAME_STATE_PAUSED:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					brkState = GAME_STATE_RUNNING;
					brkLastUpdate = ev->time;
					brkDraw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_GAME_OVER:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					brkReset();
					brkState = GAME_STATE_RUNNING;
					brkLastUpdate = ev->time;
					brkDraw();
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
					brkState = GAME_STATE_RUNNING;
					brkLastUpdate = ev->time;
					brkDraw();
				}
				return MENU_STATUS_SUCCESS;
		}
	}

	// Fixed-rate physics, independent of how often the menu tick runs.
	if (brkState == GAME_STATE_RUNNING)
	{
		if ((uint32_t)(ev->time - brkLastUpdate) >= BRK_STEP_MS)
		{
			brkLastUpdate = ev->time;
			brkPhysics(ev->time);
			brkDraw();
		}
	}

	return MENU_STATUS_SUCCESS;
}

// ---------------------------------------------------------------------------
// Game 4: 2048
// Cells hold the EXPONENT (1 = 2, 2 = 4 ... 11 = 2048), which keeps the board
// in 16 bytes and makes merging a simple increment.
// ---------------------------------------------------------------------------
#define G48_CELL   24
#define G48_GAP    2
#define G48_X0     ((DISPLAY_SIZE_X - ((4 * G48_CELL) + (5 * G48_GAP))) / 2)
#define G48_Y0     17
#define G48_WIN_EXP 11   // 2^11 = 2048

static uint8_t  g48Board[4][4];
static uint8_t  g48Undo[4][4];
static uint32_t g48Score = 0;
static uint32_t g48UndoScore = 0;
static bool     g48UndoValid = false;
static bool     g48UndoWon = false;
static bool     g48Won = false;
static bool     g48ShowWin = false;
static bool     g48NewRecord = false;
static GameState g48State = GAME_STATE_START;

static bool g48Spawn(void)
{
	int empty = 0;
	int pick;

	for (int r = 0; r < 4; r++)
	{
		for (int c = 0; c < 4; c++)
		{
			if (g48Board[r][c] == 0)
			{
				empty++;
			}
		}
	}
	if (empty == 0)
	{
		return false;
	}

	pick = (int)(gameRand() % (uint32_t) empty);
	for (int r = 0; r < 4; r++)
	{
		for (int c = 0; c < 4; c++)
		{
			if (g48Board[r][c] == 0)
			{
				if (pick == 0)
				{
					// mostly 2, occasionally 4
					g48Board[r][c] = (((gameRand() % 10U) == 0U) ? 2 : 1);
					return true;
				}
				pick--;
			}
		}
	}
	return false;
}

static void g48Reset(void)
{
	memset(g48Board, 0, sizeof(g48Board));
	g48Score = 0;
	g48Won = false;
	g48ShowWin = false;
	g48UndoValid = false;
	g48NewRecord = false;
	g48Spawn();
	g48Spawn();
}

// Slide + merge one line towards index 0. Returns true when it changed.
static bool g48SlideLine(uint8_t *l)
{
	bool changed = false;
	int w = 0;

	for (int i = 0; i < 4; i++) // compact
	{
		if (l[i] != 0)
		{
			if (i != w)
			{
				l[w] = l[i];
				l[i] = 0;
				changed = true;
			}
			w++;
		}
	}

	for (int i = 0; i < 3; i++) // merge once per tile, then close the gap
	{
		if ((l[i] != 0) && (l[i] == l[i + 1]))
		{
			l[i]++;
			g48Score += (1U << l[i]);
			if (l[i] >= G48_WIN_EXP)
			{
				g48Won = true;
			}

			for (int j = i + 1; j < 3; j++)
			{
				l[j] = l[j + 1];
			}
			l[3] = 0;
			changed = true;
		}
	}
	return changed;
}

// dir: 0 = left, 1 = right, 2 = up, 3 = down
static bool g48Move(int dir)
{
	uint8_t line[4];
	bool changed = false;

	for (int i = 0; i < 4; i++)
	{
		for (int k = 0; k < 4; k++)
		{
			switch (dir)
			{
				case 0: line[k] = g48Board[i][k];       break;
				case 1: line[k] = g48Board[i][3 - k];   break;
				case 2: line[k] = g48Board[k][i];       break;
				default: line[k] = g48Board[3 - k][i];  break;
			}
		}

		if (g48SlideLine(line))
		{
			changed = true;
		}

		for (int k = 0; k < 4; k++)
		{
			switch (dir)
			{
				case 0: g48Board[i][k] = line[k];       break;
				case 1: g48Board[i][3 - k] = line[k];   break;
				case 2: g48Board[k][i] = line[k];       break;
				default: g48Board[3 - k][i] = line[k];  break;
			}
		}
	}
	return changed;
}

static bool g48MovesLeft(void)
{
	for (int r = 0; r < 4; r++)
	{
		for (int c = 0; c < 4; c++)
		{
			if (g48Board[r][c] == 0)
			{
				return true;
			}
			if ((c < 3) && (g48Board[r][c] == g48Board[r][c + 1]))
			{
				return true;
			}
			if ((r < 3) && (g48Board[r][c] == g48Board[r + 1][c]))
			{
				return true;
			}
		}
	}
	return false;
}

static uint16_t g48TileColour(int e)
{
	switch (e)
	{
		case 1:
		case 2:  return gamePal.gameObject1;
		case 3:
		case 4:  return gamePal.gameObject2;
		case 5:
		case 6:  return gamePal.gameObject3;
		case 7:
		case 8:  return gamePal.accent;
		case 9:
		case 10: return gamePal.warning;
		default: return gamePal.danger; // 2048 and above
	}
}

static void g48Draw(void)
{
	char buf[24];

	displayClearBuf();
	gameClearScreen();
	gameDrawHeader("2048", g48Score);

	for (int r = 0; r < 4; r++)
	{
		for (int c = 0; c < 4; c++)
		{
			int x = G48_X0 + G48_GAP + (c * (G48_CELL + G48_GAP));
			int y = G48_Y0 + G48_GAP + (r * (G48_CELL + G48_GAP));
			int e = g48Board[r][c];

			if (e == 0)
			{
				gameSetColour(gamePal.shadow);
				displayFillRect(x, y, G48_CELL, G48_CELL, false);
				continue;
			}

			gameSetColour(g48TileColour(e));
			displayFillRect(x, y, G48_CELL, G48_CELL, false);

			// Higher tiles get an inner frame, so they differ by shape too.
			if (e >= 7)
			{
				gameSetColour(gamePal.background);
				displayFillRect((x + 1), (y + 1), (G48_CELL - 2), 1, false);
				displayFillRect((x + 1), (y + G48_CELL - 2), (G48_CELL - 2), 1, false);
				displayFillRect((x + 1), (y + 1), 1, (G48_CELL - 2), false);
				displayFillRect((x + G48_CELL - 2), (y + 1), 1, (G48_CELL - 2), false);
			}

			// Number, shrunk so even 4 digits stay inside the tile.
			snprintf(buf, sizeof(buf), "%u", (unsigned)(1U << e));
			{
				int len = (int) strlen(buf);
				ucFont_t f = ((len <= 2) ? FONT_SIZE_3 : FONT_SIZE_1);
				int cw = ((len <= 2) ? 8 : 6);
				int tx = x + ((G48_CELL - (len * cw)) / 2);
				int ty = y + ((G48_CELL - 8) / 2);

				gameSetColour(gamePal.foreground);
				displayPrintAt(((tx < x) ? x : tx), ty, buf, f);
			}
		}
	}

	if (g48ShowWin)
	{
		gameDrawPanel("2048", GT("Kazandin!", "You win!", "Ganaste!", "\xCF\xEE\xE1\xE5\xE4\xE0!"), NULL, GT("Yesil: Devam et", "Green: Continue", "Verde: Seguir", "\xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5"), gamePal.success);
	}

	switch (g48State)
	{
		case GAME_STATE_START:
			gameDrawPanel("2048", GT("2/8 Yukari-Asagi", "2/8 Up-Down", "2/8 Arriba-Abajo", "2/8 \xC2\xE2\xE5\xF0\xF5-\xC2\xED\xE8\xE7"), GT("4/6 Sol-Sag   # Geri al", "4/6 Left-Right  # Undo", "4/6 Izq-Der  # Deshacer", "4/6 \xCB\xE5\xE2\xEE-\xCF\xF0\xE0\xE2\xEE # \xCE\xF2\xEC\xE5\xED\xE0"),
					GT("Yesil: Basla   Kirmizi: Geri", "Green: Start   Red: Back", "Verde: Jugar   Rojo: Atr\xE1s", "\xC7\xE5\xEB: \xD1\xF2\xE0\xF0\xF2 \xCA\xF0\xE0\xF1: \xCD\xE0\xE7\xE0\xE4"), gamePal.accent);
			break;

		case GAME_STATE_PAUSED:
			gameDrawPanel(GT("DURAKLATILDI", "PAUSED", "PAUSA", "\xCF\xC0\xD3\xC7\xC0"), GT("2/8/4/6 Kaydir", "2/8/4/6 Slide", "2/8/4/6 Mover", "2/8/4/6 \xD1\xE4\xE2\xE8\xE3"), GT("# Son hamleyi geri al", "# Undo last move", "# Deshacer jugada", "# \xCE\xF2\xEC\xE5\xED\xE8\xF2\xFC \xF5\xEE\xE4"),
					GT("* veya Yesil: Devam", "* or Green: Resume", "* o Verde: Seguir", "* \xE8\xEB\xE8 \xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5"), gamePal.warning);
			break;

		case GAME_STATE_GAME_OVER:
			{
				char b2[24];

				snprintf(buf, sizeof(buf), GT("Puan: %u", "Score: %u", "Puntos: %u", "\xCE\xF7\xEA\xE8: %u"), (unsigned) g48Score);
				snprintf(b2, sizeof(b2), GT("Rekor: %u", "Best: %u", "M\xE1x: %u", "\xD0\xE5\xEA\xEE\xF0\xE4: %u"), (unsigned) gameScoreGet(GAME_ID_2048));
				gameDrawPanel((g48NewRecord ? GT("YENI REKOR", "NEW RECORD", "R\xC9""CORD!", "\xD0\xC5\xCA\xCE\xD0\xC4!") : GT("OYUN BITTI", "GAME OVER", "FIN DEL JUEGO", "\xCA\xCE\xCD\xC5\xD6 \xC8\xC3\xD0\xDB")), buf, b2,
						GT("Yesil: Tekrar   Kirmizi: Cikis", "Green: Again   Red: Exit", "Verde: Otra  Rojo: Salir", "\xC7\xE5\xEB: \xC5\xF9\xB8  \xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), (g48NewRecord ? gamePal.success : gamePal.danger));
			}
			break;

		case GAME_STATE_EXIT_CONFIRM:
			gameDrawPanel(GT("CIKIS", "EXIT", "SALIR", "\xC2\xDB\xD5\xCE\xC4"), GT("Oyundan cikilsin mi?", "Leave the game?", "Salir del juego?", "\xC2\xFB\xE9\xF2\xE8 \xE8\xE7 \xE8\xE3\xF0\xFB?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2"), gamePal.warning);
			break;

		default:
			break;
	}

	displayRender();
}

static void g48DoMove(int dir)
{
	uint8_t before[4][4];
	uint32_t scoreBefore = g48Score;
	bool wonBefore = g48Won;

	memcpy(before, g48Board, sizeof(before));

	if (!g48Move(dir))
	{
		// Nothing moved: no new tile, and the undo history stays untouched.
		g48Score = scoreBefore;
		return;
	}

	// Only a real move updates the single-level undo.
	memcpy(g48Undo, before, sizeof(g48Undo));
	g48UndoScore = scoreBefore;
	g48UndoWon = wonBefore;
	g48UndoValid = true;

	g48Spawn();
	gameBeep(MELODY_KEY_BEEP);

	if (g48Won && !wonBefore)
	{
		g48ShowWin = true; // announce once, the player may carry on
		gameBeep(MELODY_ACK_BEEP);
	}

	if (!g48MovesLeft())
	{
		g48State = GAME_STATE_GAME_OVER;
		g48NewRecord = gameScoreSubmit(GAME_ID_2048, g48Score);
		gameBeep(g48NewRecord ? MELODY_ACK_BEEP : MELODY_NACK_BEEP);
	}
}

menuStatus_t menuGame2048(uiEvent_t *ev, bool isFirstRun)
{
	if (isFirstRun)
	{
		gamePaletteBuild();
		gameScoresLoad();
		gameRandSeed();
		g48Reset();
		g48State = GAME_STATE_START;
		g48Draw();
		return MENU_STATUS_SUCCESS;
	}

	if (ev->hasEvent)
	{
		if (KEYCHECK_LONGDOWN(ev->keys, KEY_RED) && (KEYCHECK_LONGDOWN_REPEAT(ev->keys, KEY_RED) == false))
		{
			menuSystemPopPreviousMenu();
			return MENU_STATUS_SUCCESS;
		}

		// The win notice sits on top of everything until acknowledged.
		if (g48ShowWin)
		{
			if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
			{
				g48ShowWin = false;
				g48Draw();
			}
			return MENU_STATUS_SUCCESS;
		}

		switch (g48State)
		{
			case GAME_STATE_START:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					g48State = GAME_STATE_RUNNING;
					g48Draw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_RUNNING:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR))
				{
					g48State = GAME_STATE_PAUSED;
					g48Draw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					g48State = GAME_STATE_EXIT_CONFIRM;
					g48Draw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP(ev->keys, KEY_HASH)) // undo one move
				{
					if (g48UndoValid)
					{
						memcpy(g48Board, g48Undo, sizeof(g48Board));
						g48Score = g48UndoScore;
						g48Won = g48UndoWon;
						g48UndoValid = false;
						gameBeep(MELODY_KEY_BEEP);
					}
					else
					{
						gameBeep(MELODY_NACK_BEEP);
					}
					g48Draw();
					return MENU_STATUS_SUCCESS;
				}
				if (KEYCHECK_SHORTUP_NUMBER(ev->keys))
				{
					switch (ev->keys.key)
					{
						case '2': g48DoMove(2); break; // up
						case '8': g48DoMove(3); break; // down
						case '4': g48DoMove(0); break; // left
						case '6': g48DoMove(1); break; // right
						default: break;
					}
					g48Draw();
					return MENU_STATUS_SUCCESS;
				}
				break;

			case GAME_STATE_PAUSED:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_STAR) || KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					g48State = GAME_STATE_RUNNING;
					g48Draw();
				}
				else if (KEYCHECK_SHORTUP(ev->keys, KEY_RED))
				{
					menuSystemPopPreviousMenu();
				}
				return MENU_STATUS_SUCCESS;

			case GAME_STATE_GAME_OVER:
				if (KEYCHECK_SHORTUP(ev->keys, KEY_GREEN))
				{
					g48Reset();
					g48State = GAME_STATE_RUNNING;
					g48Draw();
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
					g48State = GAME_STATE_RUNNING;
					g48Draw();
				}
				return MENU_STATUS_SUCCESS;
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
			gameDrawPanel("DINO", GT("2 veya Yesil: Zipla", "2 or Green: Jump", "2 o Verde: Saltar", "2 \xE8\xEB\xE8 \xC7\xE5\xEB: \xCF\xF0\xFB\xE6\xEE\xEA"), GT("8: Egil     * Duraklat", "8: Duck     * Pause", "8: Agachar  * Pausa", "8: \xCF\xF0\xE8\xF1\xE5\xF1\xF2\xFC * \xCF\xE0\xF3\xE7\xE0"),
					GT("Yesil: Basla   Kirmizi: Geri", "Green: Start   Red: Back", "Verde: Jugar   Rojo: Atr\xE1s", "\xC7\xE5\xEB: \xD1\xF2\xE0\xF0\xF2 \xCA\xF0\xE0\xF1: \xCD\xE0\xE7\xE0\xE4"), gamePal.accent);
			break;

		case GAME_STATE_PAUSED:
			gameDrawPanel(GT("DURAKLATILDI", "PAUSED", "PAUSA", "\xCF\xC0\xD3\xC7\xC0"), NULL, GT("* veya Yesil: Devam", "* or Green: Resume", "* o Verde: Seguir", "* \xE8\xEB\xE8 \xC7\xE5\xEB: \xC4\xE0\xEB\xFC\xF8\xE5"), GT("Kirmizi: Cikis", "Red: Exit", "Rojo: Salir", "\xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), gamePal.warning);
			break;

		case GAME_STATE_GAME_OVER:
			{
				char b2[24];

				snprintf(buf, sizeof(buf), GT("Puan: %u", "Score: %u", "Puntos: %u", "\xCE\xF7\xEA\xE8: %u"), (unsigned) dinoScore);
				snprintf(b2, sizeof(b2), GT("Rekor: %u", "Best: %u", "M\xE1x: %u", "\xD0\xE5\xEA\xEE\xF0\xE4: %u"), (unsigned) gameScoreGet(GAME_ID_DINO));
				gameDrawPanel((dinoNewRecord ? GT("YENI REKOR", "NEW RECORD", "R\xC9""CORD!", "\xD0\xC5\xCA\xCE\xD0\xC4!") : GT("OYUN BITTI", "GAME OVER", "FIN DEL JUEGO", "\xCA\xCE\xCD\xC5\xD6 \xC8\xC3\xD0\xDB")), buf, b2,
						GT("Yesil: Tekrar   Kirmizi: Cikis", "Green: Again   Red: Exit", "Verde: Otra  Rojo: Salir", "\xC7\xE5\xEB: \xC5\xF9\xB8  \xCA\xF0\xE0\xF1: \xC2\xFB\xF5\xEE\xE4"), (dinoNewRecord ? gamePal.success : gamePal.danger));
			}
			break;

		case GAME_STATE_EXIT_CONFIRM:
			gameDrawPanel(GT("CIKIS", "EXIT", "SALIR", "\xC2\xDB\xD5\xCE\xC4"), GT("Oyundan cikilsin mi?", "Leave the game?", "Salir del juego?", "\xC2\xFB\xE9\xF2\xE8 \xE8\xE7 \xE8\xE3\xF0\xFB?"), NULL, GT("Yesil: Evet   Kirmizi: Hayir", "Green: Yes   Red: No", "Verde: S\xED   Rojo: No", "\xC7\xE5\xEB: \xC4\xE0   \xCA\xF0\xE0\xF1: \xCD\xE5\xF2"), gamePal.warning);
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
