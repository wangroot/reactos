/*
 * internal executive prototypes
 */

#ifndef __NTOSKRNL_INCLUDE_INTERNAL_EXECUTIVE_H
#define __NTOSKRNL_INCLUDE_INTERNAL_EXECUTIVE_H

typedef enum
{
  wmCenter = 0,
  wmTile,
  wmStretch
} WALLPAPER_MODE;

typedef struct _WINSTATION_OBJECT
{
  CSHORT Type;
  CSHORT Size;
  KSPIN_LOCK Lock;
  UNICODE_STRING Name;
  LIST_ENTRY DesktopListHead;
  PRTL_ATOM_TABLE AtomTable;
  PVOID HandleTable;
  HANDLE SystemMenuTemplate;
  PVOID SystemCursor;
  UINT CaretBlinkRate;
  HANDLE ShellWindow;
  HANDLE ShellListView;

  /* Wallpaper */
  HANDLE hbmWallpaper;
  ULONG cxWallpaper, cyWallpaper;
  WALLPAPER_MODE WallpaperMode;
  
  ULONG Flags;
  struct _DESKTOP_OBJECT* ActiveDesktop;
  /* FIXME: Clipboard */
  LIST_ENTRY HotKeyListHead;
  FAST_MUTEX HotKeyListLock;
} WINSTATION_OBJECT, *PWINSTATION_OBJECT;

typedef struct _DESKTOP_OBJECT
{
  CSHORT Type;
  CSHORT Size;
  LIST_ENTRY ListEntry;
  KSPIN_LOCK Lock;
  UNICODE_STRING Name;
  /* Pointer to the associated window station. */
  struct _WINSTATION_OBJECT *WindowStation;
  /* Pointer to the active queue. */
  PVOID ActiveMessageQueue;
  /* Rectangle of the work area */
#ifdef __WIN32K__
  RECT WorkArea;
#else
  LONG WorkArea[4];
#endif
  /* Handle of the desktop window. */
  HANDLE DesktopWindow;
  HANDLE PrevActiveWindow;
  /* Thread blocking input */
  PVOID BlockInputThread;
} DESKTOP_OBJECT, *PDESKTOP_OBJECT;

typedef struct _RUNDOWN_DESCRIPTOR {
    ULONG References;
    PKEVENT RundownEvent;
} RUNDOWN_DESCRIPTOR, *PRUNDOWN_DESCRIPTOR;

typedef VOID (*PLOOKASIDE_MINMAX_ROUTINE)(
  POOL_TYPE PoolType,
  ULONG Size,
  PUSHORT MinimumDepth,
  PUSHORT MaximumDepth);

/* GLOBAL VARIABLES *********************************************************/

TIME_ZONE_INFORMATION _SystemTimeZoneInfo;
extern POBJECT_TYPE ExEventPairObjectType;


/* INITIALIZATION FUNCTIONS *************************************************/

VOID
ExpWin32kInit(VOID);

VOID 
ExInit2 (VOID);
VOID
ExInit3 (VOID);
VOID 
ExInitTimeZoneInfo (VOID);
VOID 
ExInitializeWorkerThreads(VOID);
VOID
ExpInitLookasideLists(VOID);
VOID
ExpInitializeCallbacks(VOID);

/* OTHER FUNCTIONS **********************************************************/

#ifdef _ENABLE_THRDEVTPAIR
VOID
ExpSwapThreadEventPair(
	IN struct _ETHREAD* Thread,
	IN struct _KEVENT_PAIR* EventPair
	);
#endif /* _ENABLE_THRDEVTPAIR */

LONGLONG 
FASTCALL
ExfpInterlockedExchange64(LONGLONG volatile * Destination,
                          PLONGLONG Exchange);


#endif /* __NTOSKRNL_INCLUDE_INTERNAL_EXECUTIVE_H */
