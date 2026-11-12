project(
    'openQ4',
    ['c', 'cpp'],
    version: '1.1.1',
    meson_version: '>=1.6.0',
    default_options: [
        'c_std=c11',
        'cpp_std=c++20',
        'b_vscrt=static_from_buildtype',
    ],
)

host_system = host_machine.system()
host_cpu_family = host_machine.cpu_family()

binary_arch = host_cpu_family
if host_cpu_family == 'x86_64'
    binary_arch = 'x64'
elif host_cpu_family == 'x86'
    binary_arch = 'x86'
elif host_cpu_family == 'aarch64'
    binary_arch = 'arm64'
endif
game_sp_binary_name = 'game-sp_' + binary_arch
game_mp_binary_name = 'game-mp_' + binary_arch
game_module_filename_suffix = '.so'
engine_client_binary_name = meson.project_name() + '-client_' + binary_arch
module_filename_prefix = ''

if host_cpu_family != 'x86_64' and not (
    true
    and host_cpu_family == 'aarch64'
)
    warning(
        'openQ4 currently targets x64 as the active compatibility baseline. ' + 'Non-x64 builds are considered experimental during platform bring-up.',
    )
endif

platform_backend = 'sdl3'



use_sdl3_backend = true

py = find_program('python3', 'python', native: true, required: true)
fs_mod = import('fs')

version_track = get_option('version_track').strip()
version_iteration = get_option('version_iteration').strip()
version_base_override = get_option('version_base_override').strip()
generated_version_header_path = meson.current_build_dir() / 'openq4_version_generated.h'
version_metadata_command = [
    py,
    meson.project_source_root() / 'tools' / 'build' / 'openq4_version.py',
    '--source-root',
    meson.project_source_root(),
    '--track',
    version_track,
]
# Release builds pin the exact version. Everything else lets the helper resolve
# it: project(version:) above is only a floor, raised to the newest published v*
# tag when git history is available, so an unpinned build never advertises a
# version that was never released.
if version_base_override != ''
    version_metadata_command += ['--base-version', version_base_override]
endif
if version_iteration != ''
    version_metadata_command += ['--iteration', version_iteration]
endif
version_metadata_result = run_command(
    version_metadata_command + ['--header-out', generated_version_header_path],
    check: true,
)

# Git metadata changes whenever HEAD or the worktree changes, without changing
# meson.build. Re-run the lightweight generator for every build invocation;
# write_if_changed keeps downstream recompilation limited to real version
# changes. The configure-time invocation above ensures the header also exists
# before a backend performs its first dependency scan.
generated_version_header = custom_target(
    'openq4_version_generated',
    output: 'openq4_version_generated.h',
    command: version_metadata_command + ['--header-out', '@OUTPUT@'],
    depend_files: files('tools/build/openq4_version.py'),
    build_always_stale: true,
    build_by_default: true,
    install: false,
)

openq4_version_base = meson.project_version()
openq4_version_short = meson.project_version()
openq4_version_full = meson.project_version()
openq4_version_tag = meson.project_version()
openq4_version_date = ''
openq4_product_version = meson.project_name() + ' ' + meson.project_version()
openq4_product_version_full = openq4_product_version
openq4_version_resource_dotted = meson.project_version() + '.0'
openq4_git_sha = ''
openq4_git_dirty = '0'
openq4_commit_count = '0'
root_include_dir = include_directories('.')

foreach version_line : version_metadata_result.stdout().strip().split('\n')
    if version_line == ''
        continue
    endif

    version_pair = version_line.split('=')
    if version_pair.length() != 2
        continue
    endif

    version_key = version_pair[0]
    version_value = version_pair[1]
    if version_key == 'base_version'
        openq4_version_base = version_value
    elif version_key == 'version_short'
        openq4_version_short = version_value
    elif version_key == 'version'
        openq4_version_full = version_value
    elif version_key == 'version_tag'
        openq4_version_tag = version_value
    elif version_key == 'version_date'
        openq4_version_date = version_value
    elif version_key == 'product_version'
        openq4_product_version = version_value
    elif version_key == 'product_version_full'
        openq4_product_version_full = version_value
    elif version_key == 'resource_dotted'
        openq4_version_resource_dotted = version_value
    elif version_key == 'git_sha'
        openq4_git_sha = version_value
    elif version_key == 'git_dirty'
        openq4_git_dirty = version_value
    elif version_key == 'commit_count'
        openq4_commit_count = version_value
    endif
endforeach

build_engine = get_option('build_engine')
build_games = get_option('build_games')
build_game_sp = get_option('build_game_sp')
build_game_mp = get_option('build_game_mp')
needs_game_headers = build_engine or build_games
install_root_dir = meson.project_source_root() / '.install'
install_game_dir = install_root_dir / 'baseoq4'
install_module_dir = install_game_dir
install_engine_dir = install_root_dir
game_libs_repo_root = meson.project_source_root()
game_libs_source_root = game_libs_repo_root / 'src'
game_libs_repo_summary = 'in-tree (src/game and src/mpgame)'
game_stage_root = meson.project_source_root()
game_stage_game_root = game_stage_root / 'src' / 'game'
game_stage_mpgame_root = game_stage_root / 'src' / 'mpgame'
game_stage_game_include_rel = 'src/game'
game_stage_mpgame_include_rel = 'src/mpgame'
bse_source_root = meson.project_source_root() / 'src' / 'bse'
game_sp_module_defs_file = ''
game_mp_module_defs_file = ''

if not build_engine and not build_games
    error(
        'At least one build target must be enabled. Use -Dbuild_engine=true and/or -Dbuild_games=true.',
    )
endif


# The retained runtime is isolated from engine PCH/platform/render headers.
# It can be tested with the same layout/geometry code without creating a window.
if build_engine
    rmlui_project = subproject('rmlui')
    jsoncpp_project = subproject('jsoncpp')
    libtess2_project = subproject('libtess2')
    openq4_retained_library = static_library(
        'openq4-retained-ui',
        files(
            'src/ui/retained/Runtime.cpp',
            'src/ui/retained/Document.cpp',
            'src/ui/retained/DocumentEdit.cpp',
            'src/ui/retained/Presentation.cpp',
            'src/ui/retained/Behavior.cpp',
            'src/ui/retained/State.cpp',
            'src/ui/retained/Motion.cpp',
            'src/ui/retained/Interaction.cpp',
            'src/ui/retained/ValueControlView.cpp',
            'src/ui/retained/NumberControlView.cpp',
            'src/ui/retained/Input.cpp',
            'src/ui/retained/TextInput.cpp',
            'src/ui/retained/TextEdit.cpp',
            'src/ui/retained/TextEditCommand.cpp',
            'src/ui/retained/TextInputBroker.cpp',
            'src/ui/retained/TextRun.cpp',
            'src/ui/retained/Vector.cpp',
            'src/ui/retained/VectorElement.cpp',
            'src/ui/retained/HudElement.cpp',
            'src/ui/application/SettingsTransaction.cpp',
            'src/ui/application/SettingsJournal.cpp',
            'src/ui/application/SettingsEffectPlan.cpp',
            'src/ui/application/SettingsDisplayController.cpp',
        ),
        files('src/ui/retained/TextInputBrokerWire.cpp'),
        files('src/ui/retained/ModelScene.cpp'),
        files('src/ui/retained/PlayerSetupRequest.cpp'),
        files('src/ui/retained/NativeTextDocument.cpp'),
        files('src/ui/retained/NativeTextEditor.cpp'),
        files('src/ui/retained/ScrollGeometry.cpp'),
        files('src/ui/retained/ScrollbarView.cpp'),
        files('src/ui/retained/PopupPlacement.cpp'),
        dependencies: [
            rmlui_project.get_variable('rmlui_dep'),
            jsoncpp_project.get_variable('jsoncpp_dep'),
            libtess2_project.get_variable('libtess2_dep'),
        ],
        pic: true,
        install: false,
    )
    openq4_retained_dep = declare_dependency(link_with: openq4_retained_library)
    # Actual Windows SDK interfaces stay outside the portable runtime and engine
    # PCH. This object has no native activation, SDL or dedicated-server dependency.
endif

openq4_pak_files = []
openq4_paks_generated_header = []
if build_engine or build_games
    openq4_pak_script_files = files(
        'tools/build/openq4_pak.py',
        'tools/build/content_packs.json',
    )
    openq4_pak_manifest_script_files = files(
        'tools/build/write_pak_manifest.py',
    )

    # tools/build/content_packs.json lists the runtime packs; each gets a source
    # manifest target and a pack target, and the generated header embeds every
    # pack's digest into the engine.
    # Reading the table through fs makes Meson reconfigure when it changes, so an
    # overlay added or removed there gains or loses its target.
    fs_mod.read('tools/build/content_packs.json')
    openq4_content_pack_table = run_command(
        py,
        meson.project_source_root() / 'tools' / 'build' / 'content_packs.py',
        'meson-list',
        check: true,
    ).stdout().strip().split('\n')
    openq4_pak_header_args = []
    # A release passes -Dprebuilt_content_packs=<dir>: every platform then copies
    # the release's single build of each pack instead of repacking it.
    openq4_prebuilt_pack_args = []
    if get_option('prebuilt_content_packs') != ''
        openq4_prebuilt_pack_args = [
            '--prebuilt-dir',
            get_option('prebuilt_content_packs'),
        ]
    endif
    foreach openq4_pack_row : openq4_content_pack_table
        openq4_pack_fields = openq4_pack_row.strip().split('|')
        openq4_pack_name = openq4_pack_fields[0]
        openq4_pack_source = openq4_pack_fields[1]
        openq4_pack_stem = openq4_pack_fields[2]
        openq4_pack_manifest = custom_target(
            'openq4_' + openq4_pack_stem + '_source_manifest',
            output: openq4_pack_stem + '.sources',
            command: [
                py,
                meson.project_source_root() / 'tools' / 'build' / 'write_pak_manifest.py',
                meson.project_source_root(),
                openq4_pack_name,
                openq4_pack_source,
                '@OUTPUT@',
            ],
            depend_files: openq4_pak_script_files + openq4_pak_manifest_script_files,
            build_always_stale: true,
            build_by_default: true,
        )
        openq4_pack_output = custom_target(
            'openq4_' + openq4_pack_stem,
            input: openq4_pack_manifest,
            output: openq4_pack_name,
            command: [
                py,
                meson.project_source_root() / 'tools' / 'build' / 'build_openq4_pack.py',
                '--pak-name',
                openq4_pack_name,
                '--source-dir',
                meson.project_source_root() / openq4_pack_source,
                '--manifest',
                '@INPUT@',
                '--out',
                '@OUTPUT@',
                '--stage-out',
                meson.current_build_dir() / 'baseoq4' / openq4_pack_name,
                '--source-root',
                meson.project_source_root(),
            ] + openq4_prebuilt_pack_args,
            depend_files: openq4_pak_script_files + files(
                'tools/build/build_openq4_pack.py',
            ),
            build_by_default: true,
            install: true,
            install_dir: install_game_dir,
        )
        openq4_pak_files += [openq4_pack_output]
        openq4_pak_header_args += [
            '--pack',
            openq4_pack_name + '=@INPUT' + (
                openq4_pak_files.length() - 1
            ).to_string() + '@',
        ]
    endforeach
    openq4_paks_header_output = custom_target(
        'openq4_paks_generated_header',
        input: openq4_pak_files,
        output: 'openq4_paks_generated.h',
        command: [
            py,
            meson.project_source_root() / 'tools' / 'build' / 'generate_pak_header.py',
        ] + openq4_pak_header_args + [
            '--header-out',
            '@OUTPUT@',
        ],
        depend_files: openq4_pak_script_files + files(
            'tools/build/generate_pak_header.py',
            'tools/build/content_packs.json',
        ),
        build_by_default: true,
        install: false,
    )
    openq4_paks_generated_header = [openq4_paks_header_output]
endif

if needs_game_headers
    foreach game_tree : ['game', 'mpgame']
        if not fs_mod.is_dir(game_libs_source_root / game_tree)
            error(
                'Canonical game source directory not found: ' + game_libs_source_root / game_tree,
            )
        endif
    endforeach
    game_sp_module_defs_file = game_stage_game_root / 'Game.def'
    game_mp_module_defs_file = game_stage_mpgame_root / 'mpgame.def'
    game_source_inventory = configure_file(
        output: 'openq4_game_sources.json',
        command: [
            py,
            files('tools/build/game_source_inventory.py'),
            meson.project_source_root(),
        ],
        capture: true,
    )
endif

# Awakening SP is built into game_sp; q4xbase now supplies content only.
awakening_summary = 'built into game_sp (user-supplied campaign content)'

if build_engine and not fs_mod.is_dir(bse_source_root)
    error('Integrated BSE source directory not found: ' + bse_source_root)
endif

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

linux_gl_dep = disabler()
linux_x11_option = get_option('linux_x11')
if build_engine and true
    if linux_x11_option.disabled()
        linux_gl_dep = cc.find_library('OpenGL', required: true)
    else
        linux_gl_dep = cc.find_library('OpenGL', required: false)
        if not linux_gl_dep.found()
            linux_gl_dep = cc.find_library('GL', required: true)
        endif
    endif
endif

# Phase B8 F3: on SDL3 Windows/Linux the client executable sheds the
# statically linked renderer and always loads the renderer-gl module
# ('gl' resolves to the module there). macOS and the legacy backends keep
# the static renderer; the dedicated targets keep it everywhere until the
# server front-end story changes.
renderer_module_only_client = build_engine and use_sdl3_backend

engine_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
    '--host-system',
    host_system,
    '--platform-backend',
    platform_backend,
    '--include-game',
    'false',
    '--renderer',
    renderer_module_only_client ? 'module' : 'static',
    check: true,
)

engine_source_paths = engine_source_discovery.stdout().strip().split('\n')
engine_source_paths += [
    'src/ui/application/SystemSettingsHost.cpp',
    'src/ui/application/SystemDisplay.cpp',
]
if engine_source_paths.length() == 0
    error('tools/build/meson_sources.py returned no engine source files.')
endif

imagetools_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
    '--emit',
    'imagetools',
    check: true,
)
imagetools_source_paths = imagetools_source_discovery.stdout().strip().split(
    '\n',
)
if imagetools_source_paths.length() == 0
    error('tools/build/meson_sources.py returned no imagetools source files.')
endif
openq4_imagetools_sources = files(imagetools_source_paths)

render_geo_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
    '--emit',
    'render_geo',
    check: true,
)
render_geo_source_paths = render_geo_source_discovery.stdout().strip().split(
    '\n',
)
if render_geo_source_paths.length() == 0
    error('tools/build/meson_sources.py returned no render_geo source files.')
endif
openq4_render_geo_sources = files(render_geo_source_paths)

renderer_gl_source_discovery = run_command(
    py,
    meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
    '--emit',
    'renderer_gl',
    check: true,
)
renderer_gl_source_paths = renderer_gl_source_discovery.stdout().strip().split(
    '\n',
)
if renderer_gl_source_paths.length() == 0
    error('tools/build/meson_sources.py returned no renderer_gl source files.')
endif
openq4_renderer_gl_sources = files(renderer_gl_source_paths)

openq4_engine_sources = files(engine_source_paths)
if build_engine
    openq4_engine_sources += [
        openq4_paks_generated_header,
        generated_version_header,
    ]
endif

game_sp_sources = []
game_mp_sources = []
game_sources = []
bse_sources = []
game_idlib_sources = []
if build_games
    game_sp_source_discovery = run_command(
        py,
        meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
        game_stage_root,
        'src/game',
        'src/game/Callbacks.cpp',
        'src/game/gamesys/Callbacks.cpp',
        check: true,
    )
    game_sp_source_paths = game_sp_source_discovery.stdout().strip().split('\n')
    if game_sp_source_paths.length() == 0
        error(
            'tools/build/list_sources.py returned no single-player game source files.',
        )
    endif

    game_sp_absolute_paths = []
    foreach rel_path : game_sp_source_paths
        game_sp_absolute_paths += [game_stage_root / rel_path]
    endforeach
    game_sp_sources = files(game_sp_absolute_paths)

    game_mp_source_discovery = run_command(
        py,
        meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
        game_stage_root,
        'src/mpgame',
        'src/mpgame/Callbacks.cpp',
        'src/mpgame/gamesys/Callbacks.cpp',
        check: true,
    )
    game_mp_source_paths = game_mp_source_discovery.stdout().strip().split('\n')
    if game_mp_source_paths.length() == 0
        error(
            'tools/build/list_sources.py returned no multiplayer game source files.',
        )
    endif

    game_mp_absolute_paths = []
    foreach rel_path : game_mp_source_paths
        game_mp_absolute_paths += [game_stage_root / rel_path]
    endforeach
    game_mp_sources = files(game_mp_absolute_paths)
    game_sources = game_sp_sources + game_mp_sources

    savegame_compat_header = custom_target(
        'openq4_savegame_compat_generated',
        output: 'openq4_savegame_compat_generated.h',
        command: [
            py,
            meson.project_source_root() / 'tools' / 'build' / 'generate_savegame_compat_header.py',
            '--project-root',
            meson.project_source_root(),
            '--game-stage-root',
            game_stage_root,
            '--header-out',
            '@OUTPUT@',
        ],
        depend_files: files(engine_source_paths) + game_sources + files(
            'tools/build/generate_savegame_compat_header.py',
        ),
        install: false,
    )
    game_sp_sources += [savegame_compat_header, generated_version_header]
    game_mp_sources += [savegame_compat_header, generated_version_header]
    if build_engine
        # Session records the canonical game source stamp before tearing down
        # the active map, so the engine target must see the same generated header.
        openq4_engine_sources += [savegame_compat_header]
    endif
endif

if build_engine
    bse_source_discovery = run_command(
        py,
        meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
        meson.project_source_root(),
        'src/bse',
        check: true,
    )
    bse_source_paths = bse_source_discovery.stdout().strip().split('\n')
    if bse_source_paths.length() == 0
        error('tools/build/list_sources.py returned no BSE source files.')
    endif
    bse_sources = files(bse_source_paths)
endif

if build_engine or build_games
    game_idlib_discovery = run_command(
        py,
        meson.project_source_root() / 'tools' / 'build' / 'list_sources.py',
        meson.project_source_root(),
        'src/idlib',
        'src/idlib/math/Simd_AltiVec.cpp',
        check: true,
    )
    game_idlib_paths = game_idlib_discovery.stdout().strip().split('\n')
    if game_idlib_paths.length() == 0
        error('tools/build/list_sources.py returned no game idlib source files.')
    endif
    game_idlib_sources = files(game_idlib_paths)
endif

fallback_opt = ['default_library=static']
glew_default_options = fallback_opt
sdl3_default_options = fallback_opt
glew_default_options += ['openq4_sdl3_loader=true']
if linux_x11_option.disabled()
    sdl3_default_options += ['x11=disabled']
elif linux_x11_option.enabled()
    sdl3_default_options += ['x11=enabled']
else
    sdl3_default_options += ['x11=auto']
endif

glew_dep = disabler()
glew_dedicated_dep = disabler()
stb_vorbis_dep = disabler()
openal_dep = disabler()
sdl3_dep = disabler()
deps = []
if build_engine
    glew_sp = subproject('glew', default_options: glew_default_options)
    glew_dep = glew_sp.get_variable('glew_dep')
    glew_dedicated_dep = glew_sp.get_variable('glew_dedicated_dep')

    # openQ4's VFS callback entry point keeps compressed streams bounded without
    # extracting PK4 members. A system copy does not contain that small extension.
    stb_vorbis_dep = subproject('stb_vorbis', default_options: fallback_opt).get_variable(
        'stb_vorbis_dep',
    )

    openal_dep = dependency(
        'openal',
        required: true,
        fallback: ['openal-soft-prebuilt', 'openal_dep'],
        default_options: fallback_opt,
    )

    if not linux_x11_option.auto()
        # A system SDL's compiled video-driver set cannot be changed by a Meson
        # feature option. Explicit linux_x11 modes therefore select the bundled
        # SDL so enabled/disabled is a real build contract rather than a hint
        # that silently depends on the distribution's SDL configuration.
        sdl3_subproject = subproject(
            'sdl3',
            default_options: sdl3_default_options,
        )
        sdl3_dep = sdl3_subproject.get_variable('sdl3_dep')
    else
        sdl3_dep = dependency(
            'sdl3',
            version: '>=3.4.4',
            required: true,
            fallback: ['sdl3', 'sdl3_dep'],
            default_options: sdl3_default_options,
        )
    endif

    deps = [glew_dep, openal_dep, stb_vorbis_dep]

    deps += [
        dependency('threads', required: true),
        cc.find_library('dl', required: true),
        linux_gl_dep,
    ]
    deps += [sdl3_dep]
endif

client_deps = deps
if renderer_module_only_client
    # the module-only client compiles against GLEW declarations (via qgl.h in
    # the precompiled header) but links no GL: the renderer-gl module carries
    # GLEW/opengl32/libGL itself
    glew_headers_dep = glew_dep.partial_dependency(
        compile_args: true,
        includes: true,
    )
    client_deps = [glew_headers_dep, openal_dep, stb_vorbis_dep]
    client_deps += [
        dependency('threads', required: true),
        cc.find_library('dl', required: true),
        sdl3_dep,
    ]
endif

if build_engine
    client_deps += openq4_retained_dep
    # RetainedHud parses the coverage catalog directly, including on platforms
    # where the optional OpenXR loader does not supply JsonCpp transitively.
    client_deps += jsoncpp_project.get_variable('jsoncpp_dep')
endif

# Native-record translation is separate from the portable runtime and engine
# PCH. A checked package capability, not SDL's version, enables the importer.
if build_engine
    text_ingress_args = []
    text_ingress_headers = []
    # openQ4's private SDL extension headers. The bundled SDL subproject carries
    # them on its own include path; a system or Android SDL needs the overlay's
    # copy, which Meson reports as reaching into the subproject directory.
    sdl3_openq4_extension_includes = []
    if build_engine and use_sdl3_backend
        text_ingress_headers = [
            sdl3_dep.partial_dependency(compile_args: true, includes: true),
        ]
        if sdl3_dep.type_name() != 'internal'
            sdl3_openq4_extension_includes = [
                include_directories('subprojects/packagefiles/sdl3/include'),
            ]
        endif
        if cpp.compiles(
            '''
      #if !defined(OPENQ4_SDL3_TEXT_PROVENANCE) || OPENQ4_SDL3_TEXT_PROVENANCE != 1
      #error No verified private provider capability
      #endif
      #include <SDL3/SDL_openq4_text_provenance.h>
      int main() { OQ4_TextRecord record{}; return record.version; }
      ''',
            dependencies: text_ingress_headers,
            name: 'verified SDL native text provenance header',
        )
            text_ingress_args = ['-DOPENQ4_SDL_TEXT_PROVENANCE=1']
        endif
    endif
    text_ingress_library = static_library(
        'openq4-text-ingress',
        files('src/sys/sdl3/TextProvenanceIngress.cpp'),
        dependencies: [openq4_retained_dep] + text_ingress_headers,
        cpp_args: text_ingress_args,
        pic: true,
        install: false,
    )
    text_ingress_dep = declare_dependency(link_with: text_ingress_library)
    if build_engine
        client_deps += text_ingress_dep
    endif
    if build_engine and use_sdl3_backend
        native_queue_library = static_library(
            'openq4-native-queue',
            files(
                'src/sys/sdl3/NativeQueueBatch.cpp',
                'src/sys/sdl3/NativeQueueSource.cpp',
                'src/sys/sdl3/NativeEventDisposition.cpp',
            ),
            files('src/ui/application/NativeTextCollectionCoordinator.cpp'),
            files('src/ui/application/ManagedNativeTextOwner.cpp'),
            include_directories: [root_include_dir] + sdl3_openq4_extension_includes,
            dependencies: [openq4_retained_dep] + text_ingress_headers,
            cpp_args: ['-DUSE_SDL3'],
            pic: true,
            install: false,
        )
        native_queue_dep = declare_dependency(link_with: native_queue_library)
        client_deps += native_queue_dep
    endif
endif

shared_cpp_args = ['-DUSE_OPENAL', '-DGLEW_NO_GLU']
shared_c_args = ['-DGLEW_NO_GLU']
engine_link_args = []
linux_executable_pie = false
linux_module_link_hardening_args = []
linux_cross_dso_sanitizer_cpp_args = []

shared_cpp_args += ['-DID_GL_HARDLINK']
shared_c_args += ['-DID_GL_HARDLINK']


linux_compile_hardening_args = []
foreach hardening_arg : ['-fstack-protector-strong', '-D_FORTIFY_SOURCE=2']
    if cpp.has_argument(hardening_arg) and cc.has_argument(hardening_arg)
        linux_compile_hardening_args += hardening_arg
    endif
endforeach
shared_cpp_args += linux_compile_hardening_args
shared_c_args += linux_compile_hardening_args

if cpp.has_argument('-fno-sanitize=vptr')
    # Engine, game, and in-process BSE translation units consume polymorphic
    # interfaces whose key functions and RTTI live on the opposite side of a
    # runtime-loaded module boundary. UBSan's vptr metadata cannot be resolved
    # while preserving the executable/module ABI and each module's -z defs contract.
    # Keep AddressSanitizer and every other requested UBSan check enabled.
    linux_cross_dso_sanitizer_cpp_args += ['-fno-sanitize=vptr']
endif

linux_link_hardening_args = []
foreach hardening_arg : ['-Wl,-z,relro', '-Wl,-z,now', '-Wl,-z,noexecstack']
    if cpp.has_link_argument(hardening_arg)
        linux_link_hardening_args += hardening_arg
    endif
endforeach
engine_link_args += linux_link_hardening_args
linux_module_link_hardening_args += linux_link_hardening_args
linux_executable_pie = cpp.has_argument('-fPIE') and cc.has_argument('-fPIE') and cpp.has_link_argument(
    '-pie',
)

shared_cpp_args += ['-DUSE_SDL3=1']
shared_c_args += ['-DUSE_SDL3=1']

# Legacy idTech4-era code relies on implicit narrowing/pointer casts that
# modern GCC/Clang reject as hard errors without permissive mode.
shared_cpp_args += ['-fpermissive']
foreach aliasing_arg : ['-fno-strict-aliasing']
    if cpp.has_argument(aliasing_arg) and cc.has_argument(aliasing_arg)
        shared_cpp_args += aliasing_arg
        shared_c_args += aliasing_arg
    endif
endforeach
# GCC/Clang default to -ffp-contract=fast, which fuses a*b+c into an FMA
# wherever the target has one. AArch64 always does; the x86-64 SSE2 baseline
# openQ4 is validated against does not. That makes identical source produce
# different float results per architecture in collision, AAS and physics
# code, which is exactly the class of divergence behind arm64-only map and
# savegame failures. MSVC already defaults to precise contraction.
foreach fp_arg : ['-ffp-contract=off']
    if cpp.has_argument(fp_arg) and cc.has_argument(fp_arg)
        shared_cpp_args += fp_arg
        shared_c_args += fp_arg
    endif
endforeach
# src/idlib/Lib.h keys assert()/verify() off _DEBUG, which MSVC supplies via
# /MTd but GCC and Clang never do here, so idlib's ownership and alignment
# checks are dead on Linux and macOS unless this is asked for.
if get_option('idlib_asserts')
    shared_cpp_args += ['-DOPENQ4_ENABLE_IDLIB_ASSERTS=1']
    shared_c_args += ['-DOPENQ4_ENABLE_IDLIB_ASSERTS=1']
endif


pch_headers = []
game_idlib_pch_headers = []
game_module_pch_headers = []
common_header_cpp_args = []
common_header_path = meson.project_source_root() / 'src' / 'idlib' / 'precompiled.h'
if get_option('use_pch')
    pch_headers = ['src/idlib/precompiled.h']
    game_idlib_pch_headers = ['src/idlib/precompiled.h']
    game_module_pch_headers = [common_header_path]
else
    # The legacy engine and SDK sources use precompiled.h as their common
    # translation-unit prefix. Disabling compiler PCH generation must include
    # the same declarations normally rather than silently changing the source
    # contract and producing hundreds of order-dependent compile failures.
    common_header_cpp_args = ['-include', common_header_path]
endif

src_include_dir = include_directories('src')
src_idlib_include_dir = include_directories('src/idlib')
src_renderer_include_dir = include_directories('src/renderer')
src_ui_include_dir = include_directories('src/ui')
src_sys_include_dir = include_directories('src/sys')
src_bse_include_dir = include_directories('src/bse')
src_maya_import_include_dir = include_directories('src/mayaimport')
game_include_dirs = [
    root_include_dir,
    src_include_dir,
    src_idlib_include_dir,
    src_renderer_include_dir,
    src_ui_include_dir,
    src_sys_include_dir,
    src_bse_include_dir,
    src_maya_import_include_dir,
]
engine_include_dirs = [root_include_dir, src_include_dir]
game_idlib_include_dirs = [root_include_dir, src_include_dir]
game_idlib_mp_include_dirs = [root_include_dir, src_include_dir]
bse_client_include_dirs = [
    root_include_dir,
    src_include_dir,
    src_bse_include_dir,
]

if needs_game_headers
    staged_game_include_dir = include_directories(game_stage_game_include_rel)
    staged_mpgame_include_dir = include_directories(
        game_stage_mpgame_include_rel,
    )
    engine_include_dirs += [staged_game_include_dir]
    game_idlib_include_dirs += [staged_game_include_dir]
    # the multiplayer idlib flavour resolves precompiled.h's
    # "../mpgame/Game_local.h" through the staged mpgame tree
    game_idlib_mp_include_dirs += [
        staged_mpgame_include_dir,
        staged_game_include_dir,
    ]
    game_include_dirs += [staged_game_include_dir, staged_mpgame_include_dir]
    bse_client_include_dirs += [staged_game_include_dir]
endif

engine_cpp_args = shared_cpp_args + common_header_cpp_args + ['-D__DOOM_DLL__'] + linux_cross_dso_sanitizer_cpp_args
engine_c_args = shared_c_args
# client-only flavor: module-only clients compile the loader/backend for the
# shed-renderer shape (dedicated and library targets stay static-shaped)
client_engine_cpp_args = engine_cpp_args
if renderer_module_only_client
    client_engine_cpp_args += ['-DOPENQ4_RENDERER_MODULE_ONLY']
endif
bse_cpp_args = shared_cpp_args + common_header_cpp_args + linux_cross_dso_sanitizer_cpp_args
bse_c_args = shared_c_args
bse_library = disabler()

if build_engine
    openq4_engine_sources += files(
        'src/sys/sdl3/NativeInputEmissionInventory.cpp',
        'src/sys/sdl3/NativeInputTransfers.cpp',
        'src/sys/sdl3/NativeInputDriver.cpp',
    )

    # The OpenXR session, input and frame loop are client-only for the same
    # reason: the dedicated snapshot above never contains them.

    bse_library = static_library(
        'openq4_bse',
        bse_sources,
        include_directories: bse_client_include_dirs,
        cpp_args: bse_cpp_args,
        c_args: bse_c_args,
        cpp_pch: pch_headers,
        pic: true,
        install: false,
    )

    # CPU image utilities shared between the engine and (post module-split) the
    # renderer modules; compiled with engine args so codegen matches the
    # pre-carve statically-linked behavior
    imagetools_library = static_library(
        'openq4_imagetools',
        openq4_imagetools_sources,
        include_directories: engine_include_dirs,
        cpp_args: engine_cpp_args,
        c_args: engine_c_args,
        cpp_pch: pch_headers,
        pic: true,
        install: false,
    )

    # CPU render-geometry shared between dmap (engine) and the renderer;
    # compiled with engine args so codegen matches the pre-carve build
    render_geo_library = static_library(
        'openq4_render_geo',
        openq4_render_geo_sources,
        include_directories: engine_include_dirs,
        cpp_args: engine_cpp_args,
        c_args: engine_c_args,
        cpp_pch: pch_headers,
        pic: true,
        install: false,
    )
endif

if build_engine
    client_link_with = [bse_library, imagetools_library, render_geo_library]


    executable(
        engine_client_binary_name,
        openq4_engine_sources,
        include_directories: engine_include_dirs,
        dependencies: client_deps,
        link_with: client_link_with,
        cpp_args: client_engine_cpp_args,
        c_args: engine_c_args,
        cpp_pch: pch_headers,
        link_args: engine_link_args,
        pie: linux_executable_pie,
        install: true,
        install_dir: install_root_dir,
    )

endif

game_common_cpp_args = shared_cpp_args + common_header_cpp_args + ['-DGAME_DLL'] + linux_cross_dso_sanitizer_cpp_args
game_common_c_args = shared_c_args
game_target_override_options = ['cpp_std=c++17']
game_module_link_args = []
game_module_link_depends = []
game_module_link_args += linux_module_link_hardening_args
if cpp.has_link_argument('-Wl,-z,defs')
    game_module_link_args += '-Wl,-z,defs'
endif
linux_game_module_export_map = files('tools/build/linux_game_module.map')
game_module_link_args += '-Wl,--version-script=' + linux_game_module_export_map[0].full_path()
game_module_link_depends += linux_game_module_export_map

game_idlib_library = disabler()
game_idlib_library_mp = disabler()
if build_engine or build_games
    # One archive per game flavour. src/idlib/precompiled.h includes
    # mpgame/Game_local.h under -DGAME_MPAPI and game/Game_local.h otherwise, and
    # every idlib translation unit includes precompiled.h. Linking one
    # single-player-flavoured archive into both modules therefore put two
    # different definitions of the same game types into each multiplayer binary,
    # which is an ODR violation the linker resolves however it likes.
    game_idlib_library = static_library(
        'openq4_game_idlib',
        game_idlib_sources,
        include_directories: game_idlib_include_dirs,
        cpp_args: game_common_cpp_args,
        c_args: game_common_c_args,
        cpp_pch: game_idlib_pch_headers,
        override_options: game_target_override_options,
        pic: true,
        gnu_symbol_visibility: 'hidden',
        install: false,
    )
    game_idlib_library_mp = static_library(
        'openq4_game_idlib_mp',
        game_idlib_sources,
        include_directories: game_idlib_mp_include_dirs,
        cpp_args: game_common_cpp_args + ['-DGAME_MPAPI'],
        c_args: game_common_c_args,
        cpp_pch: game_idlib_pch_headers,
        override_options: game_target_override_options,
        pic: true,
        gnu_symbol_visibility: 'hidden',
        install: false,
    )
endif

renderer_gl_binary_name = 'renderer-gl_' + binary_arch

# emileb's GLES renderer: the one renderer module this fork builds.
renderer_gles_binary_name = 'renderer-gles_' + binary_arch
build_renderer_gles = build_engine
gles_root = get_option('gles_root').strip()
angle_gles_lib = disabler()
gles_deps = []
if build_renderer_gles
    gles_library_dirs = []
    if gles_root != ''
        gles_library_dirs += [gles_root / 'lib']
    endif
    angle_gles_lib = cpp.find_library(
        'GLESv2',
        dirs: gles_library_dirs,
        required: true,
    )
    gles_deps = [
        angle_gles_lib,
        cpp.find_library('EGL', dirs: gles_library_dirs, required: true),
    ]
endif
if build_engine and build_renderer_gles
    # the modules' own idlib copy, compiled with engine flavor so codegen and
    # interface layout match the renderer sources
    renderer_idlib_library = static_library(
        'openq4_renderer_idlib',
        game_idlib_sources,
        include_directories: engine_include_dirs,
        cpp_args: engine_cpp_args,
        c_args: engine_c_args,
        cpp_pch: pch_headers,
        pic: true,
        install: false,
    )
endif

# Experimental OpenGL ES renderer module.
#
# The shared renderer front-end again, but with GL resolved through
# renderer/GLES/qgl_gles.h against a real ES driver instead of GLEW against
# desktop GL. Unlike renderer-vk this KEEPS the ModernGL translation units --
# GLES is still GL, so the executor, shader library, state cache, upload
# manager and render-graph resources are reused rather than rewritten -- and
# keeps tr_backend.cpp, whose five references into the dropped legacy TUs are
# supplied by renderer/GLES/gles_Backend.cpp.
#
if build_renderer_gles
    renderer_gles_source_discovery = run_command(
        py,
        meson.project_source_root() / 'tools' / 'build' / 'meson_sources.py',
        '--emit',
        'renderer_gles',
        check: true,
    )
    renderer_gles_source_paths = renderer_gles_source_discovery.stdout().strip().split(
        '\n',
    )
    if renderer_gles_source_paths.length() == 0
        error(
            'tools/build/meson_sources.py returned no renderer_gles source files.',
        )
    endif
    renderer_gles_sources = files(renderer_gles_source_paths)

    renderer_gles_module_args = engine_cpp_args + [
        '-DOPENQ4_RENDERER_MODULE',
        '-DOPENQ4_RENDERER_GLES_MODULE',
        '-DOPENQ4_SDL3_CONTEXT_IMPL',
        '-DOPENQ4_RENDERER_BACKEND_NAME="gles"',
        '-DOPENQ4_RENDERER_BACKEND_DESC="openQ4 OpenGL ES renderer module"',
    ]

    renderer_gles_export_list = []
    renderer_gles_link_args = []
    renderer_gles_export_list = files('tools/build/linux_renderer_module.map')
    renderer_gles_link_args = linux_module_link_hardening_args + [
        '-Wl,--version-script=' + renderer_gles_export_list[0].full_path(),
        '-Wl,-z,defs',
    ]
    gles_deps += [
        cc.find_library('m', required: true),
        cc.find_library('dl', required: true),
    ]

    # shared_library, not shared_module: meson links shared_module on darwin as an
    # MH_BUNDLE with -undefined dynamic_lookup, which drops the undefined-symbol
    # closure. That closure is the point here -- it is what proves the module has
    # no desktop GL left in it.
    renderer_gles_module_target = shared_library(
        renderer_gles_binary_name,
        renderer_gles_sources,
        include_directories: [
            engine_include_dirs,
            include_directories('src/external/gles'),
        ],
        cpp_args: renderer_gles_module_args,
        c_args: engine_c_args,
        cpp_pch: pch_headers,
        link_with: [
            renderer_idlib_library,
            imagetools_library,
            render_geo_library,
        ],
        dependencies: gles_deps,
        link_args: renderer_gles_link_args,
        link_depends: renderer_gles_export_list,
        gnu_symbol_visibility: 'hidden',
        name_prefix: module_filename_prefix,
        install: true,
        install_dir: install_engine_dir,
    )
endif

if build_engine or (build_games and (build_game_sp or build_game_mp))
    subdir('content/baseoq4')
endif

if build_games and build_game_sp
    custom_target(
        'openq4_stage_game_sp_direct_run',
        input: game_sp_module_target,
        output: 'stage-game-sp-' + binary_arch + '.stamp',
        command: [
            py,
            meson.project_source_root() / 'tools' / 'build' / 'stage_direct_run_game_module.py',
            '--source',
            '@INPUT@',
            '--destination',
            meson.current_build_dir() / 'baseoq4' / (
                module_filename_prefix + game_sp_binary_name + game_module_filename_suffix
            ),
            '--stamp',
            '@OUTPUT@',
        ],
        depend_files: files('tools/build/stage_direct_run_game_module.py'),
        build_always_stale: true,
        build_by_default: true,
    )
endif



install_data(
    'LICENSE',
    'LICENSING.md',
    'CONTRIBUTORS.md',
    install_dir: install_root_dir,
)
install_data(
    'LICENSES/QUAKE-4-SDK-EULA.rtf',
    'LICENSES/DOOM-3-ADDITIONAL-TERMS.txt',
    'LICENSES/DOOM-3-BFG-ADDITIONAL-TERMS.txt',
    install_dir: install_root_dir / 'licenses',
)
meson.add_install_script(
    py,
    files('tools/build/retire_game_layers.py'),
    install_root_dir,
)
install_data(
    'LICENSES/KHRONOS-GLES-MIT.txt',
    install_dir: install_root_dir / 'licenses',
)
install_data('LICENSES/RmlUi.txt', install_dir: install_root_dir / 'licenses')
install_data('LICENSES/JsonCpp.txt', install_dir: install_root_dir / 'licenses')
install_data('LICENSES/libtess2.txt', install_dir: install_root_dir / 'licenses')
install_data('LICENSES/MD4.txt', install_dir: install_root_dir / 'licenses')

version_iteration_summary = version_iteration
if version_iteration_summary == ''
    version_iteration_summary = 'none'
endif

openq4_git_sha_summary = openq4_git_sha
if openq4_git_sha_summary == ''
    openq4_git_sha_summary = 'unavailable'
endif

summary(
    {
        'Host system': host_system,
        'Host cpu family': host_cpu_family,
        'Version track': version_track,
        'Version iteration': version_iteration_summary,
        'Version base': openq4_version_base,
        'Version short': openq4_version_short,
        'Version full': openq4_version_full,
        'Version tag': openq4_version_tag,
        'Version date': openq4_version_date,
        'Version resource': openq4_version_resource_dotted,
        'Git short SHA': openq4_git_sha_summary,
        'Git dirty': openq4_git_dirty,
        'Git commit count': openq4_commit_count,
        'C++ standard': get_option('cpp_std'),
        'Platform backend': platform_backend,
        'Build engine': build_engine,
        'engine client binary': engine_client_binary_name,
        'Integrated BSE in client': build_engine,
        'Integrated BSE source dir': bse_source_root,
        'Build games': build_games,
        'GameLibs source repo': game_libs_repo_summary,
        'Build game_sp': build_games and build_game_sp,
        'Build game_mp': build_games and build_game_mp,
        'game_sp binary': game_sp_binary_name,
        'game_mp binary': game_mp_binary_name,
        'Awakening SP campaign': awakening_summary,
        'SDL3': use_sdl3_backend and sdl3_dep.found(),
        'Linux executable PIE': linux_executable_pie,
        'GLEW': glew_dep.found(),
        'OpenAL': openal_dep.found(),
        'stb_vorbis': stb_vorbis_dep.found(),
        'Precompiled headers': get_option('use_pch'),
        'GLES renderer module': build_renderer_gles,
        'Install root': install_root_dir,
    },
    section: 'Build Features',
    bool_yn: true,
)
