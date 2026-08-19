# Tabs and Notepad-style Session Persistence Plan

## Product decisions

- Opening a file creates a new tab, unless the active tab is empty, unsaved, and not file-backed.
- Empty unsaved tabs are not restored on next launch.
- Saving a tab removes its unsaved session content. The restored tab layout should point at the saved file instead.
- Closing the app preserves open tabs automatically; users do not need to save manually.
- Closing an individual unsaved tab is an explicit discard action.

## Task tracker

### Phase 0 — Stabilize current work
- [x] Review current WIP diff in `src/Main.qml`, `src/backend.cpp`, `src/backend.h`, `src/main.cpp`.
- [x] Keep useful parts of the exploratory tab implementation.
- [x] Ensure the project builds before deeper refactoring.

### Phase 1 — Split app/window state from document/tab state
- [x] Make per-tab document state independent by using one `DocumentBackend` per tab.
- [x] Keep theme, text scale, window geometry, and session storage shared through the app-level backend.
- [x] Ensure each tab has its own file URL, modified flag, word count, highlighter, file watcher, and save status.

### Phase 2 — Session persistence model
- [x] Add a durable session/tab-layout store under `QStandardPaths::AppDataLocation`.
- [x] Store tab order and active tab index.
- [x] For unsaved tabs, store text content automatically.
- [x] For saved/file-backed clean tabs, store the file URL instead of duplicating session content.
- [x] Do not persist empty unsaved tabs.
- [x] Restore file-backed tabs from disk on launch.
- [x] Restore unsaved tabs from session content on launch.

### Phase 3 — Tab UI
- [x] Add a tab strip.
- [x] Keep `Ctrl+N` opening a new window.
- [x] Make `Ctrl+T` create a new tab.
- [x] Add `Ctrl+W` close-tab behavior.
- [x] Route title, footer status, and word count to the active tab.
- [x] Show per-tab modified markers.

### Phase 4 — Command and dialog routing
- [x] `Ctrl+S` saves the active tab.
- [x] `Ctrl+Shift+S` saves active tab as.
- [x] `Ctrl+O` opens into a new tab unless active tab is empty and unsaved.
- [x] `Ctrl+P` prints active tab.
- [x] Find/replace operates on active tab only.
- [x] External-change dialogs target the active tab.

### Phase 5 — Close behavior
- [x] App close flushes all tabs to the session store and exits without unsaved prompts.
- [x] Closing an unsaved tab discards it explicitly through close-tab.
- [x] Closing a saved clean tab removes it from the restored tab layout.
- [x] Closing a saved dirty tab keeps/restores it from session state unless discarded.

### Phase 6 — Tests
- [x] Existing tests pass.
- [x] Add backend/session persistence tests.
- [x] Add test for saved tab restoring by file URL.
- [x] Add test for unsaved tab restoring by text.
- [x] Add test that empty unsaved tabs are not restored.
- [x] Add QML smoke coverage for tab-backed editor creation and active-tab command routing.

## Current status

Implemented. Build and tests pass with Qt offscreen in `/tmp/omawrite-build` and `/tmp/omawrite-tests`.
