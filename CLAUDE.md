# arjuyDisplays

App de escritorio para Omarchy (Hyprland 0.56, config en Lua, Omarchy 4.5) para ordenar monitores como en Windows.

## Objetivo (pedido del usuario)
- Mover monitores arrastrándolos para cambiar su posición (con imán entre bordes, sin superposición ni huecos).
- Identificar cada monitor: mostrar un cartel grande con su nombre (DP-3, DP-5, eDP-1...) en cada pantalla real.
- Ajustar la resolución (y Hz) de cada monitor; también escala y rotación.
- Es una app de escritorio independiente, NO un plugin de la barra.

## Tecnología decidida
C++ + Qt Quick (QML), qmake6 + make, un solo binario, sigue el tema de Omarchy, empaquetada como paquete de Arch (PKGBUILD) para que aparezca en el lanzador y se quite con pacman -R. Seguir el skill `omarchy-app` (flujo de las apps propias de Omarchy: Hype, Monologue, Omacut). Qt 6.12 y qmake6 ya están instalados. Datos desde `hyprctl monitors -j`.

## Reglas de seguridad (obligatorias)
- Solo toca `~/.config/hypr/monitors.lua` y sus backups en `~/.local/state/arjuy-displays/`. Nunca `shell.json` ni archivos en /usr/share/omarchy.
- Al aplicar: 1) backup con fecha y hora (más una copia "original" que se guarda una sola vez y no se pisa), 2) validación previa del formato de monitors.lua y de la respuesta de hyprctl; si algo no cuadra, avisar y NO escribir (modo solo lectura), 3) reescribir solo las entradas `hl.monitor(...)` y preservar todo lo demás (p. ej. workspaces fijos por monitor), 4) recargar Hyprland y revisar `hyprctl configerrors`, 5) cuenta regresiva de 15 s para "Mantener"; si no se confirma, revertir al backup anterior. La cuenta corre dentro de la app.
- Botón "Restaurar original" antes de desinstalar. Desinstalar deja monitors.lua como esté.
- Modo solo lectura y aviso si la versión de Hyprland/Omarchy no fue probada.
- Durante el desarrollo NO modificar el monitors.lua real: probar con archivos temporales y con un monitor de respaldo conectado. La prueba real la hace el usuario.

## Fases
1. Solo lectura: ventana que muestra los monitores a escala con nombre y modos. No toca config.
2. Identificar y mover: cartel con el nombre en cada pantalla y arrastre con imán, sin aplicar.
3. Aplicar con seguridad: backup, escritura, recarga, cuenta regresiva; resolución, escala y rotación.
4. Paquete: PKGBUILD, entrada en el lanzador, "Restaurar original".

## Pendiente de verificar antes de la fase 3
- [RESUELTO] Aplicar en vivo en Hyprland 0.56 con config Lua: `hyprctl keyword` no sirve; se usa `hyprctl eval '<Lua>'` (en vivo, no persiste) y reescribir monitors.lua + `hyprctl reload` para persistir.
- [RESUELTO] Cartel de identificar: overlay LayerShellQt (layer-shell-qt 6.8.1 ya instalado), con fallback a ventana normal.

## Contexto
- Monitores actuales (monitors.lua): DP-3 en 0x0, DP-5 en 1920x0, DP-6, eDP-1 en 3840x0, escalas 1 y 1.25; hay workspaces fijos por monitor.
- Referencias existentes (no reinventar sin razón): plugin Screens de IM0001GT (github.com/IM0001GT/omarchy-screens, MIT) hace algo similar como plugin de barra; sirve de referencia de diseño y de riesgos (multi-GPU, HDR, conflictos con otros editores de layout).
- Diseño visual y flujo: artifact https://claude.ai/artifact/PQmqiDYEacZEXETLJ7pQ72
- Idioma de la UI y de la comunicación con el usuario: español.

## Decisiones y hallazgos del análisis (2026-11-12)

### Decisiones del usuario
- (a) Las entradas del monitors.lua que no son monitores conectados (regla genérica `output=""` y DP-6 desconectado) son intocables: se preservan. Solo se editan monitores conectados con entrada propia; las entradas nuevas se agregan después de la genérica.
- (b) eDP-1 usa la variable `omarchy_monitor_scale`: se preserva si no cambia su escala; si cambia, se escribe un número literal y se avisa.
- (c) Textos fuente de la UI en inglés + `src/i18n/es.json` (patrón omarchy-app).
- (d) `git init` local, sin subir a GitHub todavía.

### Resolución técnica
- `hyprctl keyword` no sirve con config Lua. Se usa `hyprctl eval '<Lua>'` para aplicar en vivo (no persiste) y reescribir monitors.lua + `hyprctl reload` para persistir (reload relee el archivo). Falta probar con monitor headless (`hyprctl output create headless`) si eval admite varias llamadas y si reload pisa lo aplicado con eval.
- Cartel de identificar: overlay LayerShellQt (layer-shell-qt 6.8.1 ya instalado), uno por QScreen, layer overlay, sin foco, scope "arjuy-displays-identify", `Shell::useLayerShell()` antes de crear QGuiApplication; enlazar con `INCLUDEPATH /usr/include` y `LIBS -lLayerShellQtInterface`. Fallback a ventana normal.
- PKGBUILD depends: qt6-base qt6-declarative qt6-wayland layer-shell-qt hyprland.
- Riesgo: plugin compilado contra Qt 6.8 corriendo en Qt 6.12.

### Hallazgos
- Los toggles de Omarchy en `~/.local/state/omarchy/toggles/hypr/*.lua` cargan DESPUÉS de monitors.lua (p. ej. internal-monitor-disable) y pueden anular cambios: detectar y avisar.
- Otros scripts de Omarchy (omarchy-hyprland-monitor-scaling) también reescriben monitors.lua: comparar hash/mtime antes de escribir; escritura atómica (temporal + rename, compatible con symlink); validar nombre de monitor con `^[A-Za-z0-9._-]+$`.
- DP-3 y DP-5 tienen el mismo make/model/serial: solo los distingue el conector.
- Versión de Omarchy con `pacman -Q omarchy` (4.5.0-3), no `/usr/share/omarchy/version`. Hyprland 0.56.2.
- Escalas válidas solo si dividen limpiamente la resolución (copiar `clean_scale` de omarchy-hyprland-monitor-scaling).
- x,y de hyprctl son coordenadas lógicas y son la fuente de verdad (Qt solo para ubicar el cartel).
- `hyprctl configerrors` vacío = sin errores (formato con errores desconocido).
- `availableModes` tiene duplicados y `refreshRate` fraccionario: normalizar.

### Reglas de reescritura de monitors.lua
- Solo líneas de una sola línea `hl.monitor({...})` con output concreto.
- Tocar solo mode, position, scale, transform.
- Preservar comentarios, `local`, `hl.env`, `hl.workspace_rule` y claves no gestionadas (vrr, cm, mirror, disabled...).
- Entradas con valores no literales, multilínea o duplicadas: no editables, con aviso.
- Validar tras escribir.

### Pruebas
- Rutas de monitors.lua y backups por parámetro/variable de entorno (p. ej. `ARJUY_MONITORS_LUA`).
- `hyprctl` falso en tests; tests con QTemporaryDir.
- Monitores headless solo para pruebas.
- El plugin layer-shell no existe en offscreen: tolerar su ausencia.

### Arrastre y detección de problemas (2026-11-12, solo vista previa)
- `src/layoutgeometry.{h,cpp}`: geometría pura (imán `snap`, empuje `settle`, `analyze` de problemas) sobre rectángulos lógicos enteros (activos, sin deshabilitados ni espejos). Contacto = borde compartido con tramo > 0 (una esquina no cuenta).
- Backend: posiciones pendientes por monitor (`pending`), undo/redo (snapshots, tope 200), `dragSnap` (vista previa viva + revalida), `dragEnd` (imán + `settle` + commit), `moveSelected` (teclado literal, sin imán: solo avisa), `resetPending` (también deshacible). `monitors` expone `x,y` efectivas más `currentX/currentY/moved/movable/warn`. Un refresh conserva lo pendiente y avisa (`pendingNotice`) si el layout real cambió. NO se aplica ni escribe nada (es la fase 3).
- `settle`: candidatos en la rejilla de bordes ajenos; prioridad 0 = toca y deja todo conectado, 1 = toca, 2 = sin solape; elige el más cercano al punto soltado.
- Problemas: hueco (con pista de escala si pixeles-lógicos == hueco), solape, aislado, grupos desconectados (solo con 2+ grupos reales), contacto parcial (solo info). Los avisos son solo lectura: el botón "Arreglar" es fase 3.
- QML: el Repeater del mapa usa `monitors.length` como modelo (no el array) para que las casillas no se recreen durante un arrastre; el arrastre usa posición absoluta del puntero (no deltas); el banner de avisos conserva su alto mientras se arrastra para que el mapa no salte. Las flechas ahora MUEVEN (Tab/Shift+Tab seleccionan).
- Bug real encontrado: `~EventWatcher` — `m_socket` se destruía después de los temporizadores y su `disconnected` tocaba `m_reconnect` ya destruido (era el segfault intermitente del test `eventWatcherDebouncesAndReconnects`). Arreglado con destructor que desconecta y aborta el socket antes.

### Espacios de trabajo (2026-11-12, solo lectura)
- `src/workspaces.{h,cpp}`: `Workspaces` (lógica pura y estática): `parseWorkspaces`/`parseRules` (JSON validado entero), `parseBlockState` (tolerante: salta líneas raras; Invalid si no hay nada útil o un bloque repetido), `readBlockState` (solo lee), `build` (combina monitores + workspaces + reglas + bloques en `WorkspaceModel`), `read` (corre `hyprctl -j workspaces` y `workspacerules`). Backend lo llama en cada `refresh()`; un fallo de workspaces NO rompe los monitores (`workspaceError`).
- Archivo de estado de Omarchy `~/.local/state/omarchy/monitor-workspaces` (override `ARJUY_OMARCHY_WORKSPACES`; los tests lo apuntan a una ruta inexistente): líneas `bloque<TAB>conector<TAB>identidad`; bloque N = ids N*10+1..N*10+10; identidad = description (o nombre) con ` #2` para gemelos; la fila con identidad `FALLBACK` no es un monitor. Se asocia por conector + identidad; gemelos sin fila de conector = bloque desconocido.
- Avisos (informativos): regla fija a monitor distinto del dueño del bloque (agrupados por par monitor/dueño), ventanas fuera del bloque, regla a monitor desconectado, id duplicado. Con el estado real: workspaces 1,2 fijados a DP-3 y 3,4 a DP-5 pertenecen al bloque de eDP-1 (1-10).
- QML: `WorkspaceChip.qml`, chips en `MonitorTile` (hijos de la casilla, siguen el arrastre; "+N" si no caben), sección en `MonitorPanel` (ahora dentro de un Flickable), banner "Workspace notes" en `Main.qml`. El refresh por evento ya no cancela un arrastre salvo que cambie el layout. `eventwatcher` escucha además los eventos workspace/createworkspace/destroyworkspace/moveworkspace (y v2).
- NO se editan workspaces: hacerlo exigiría ampliar la regla de seguridad a `hl.workspace_rule`.
