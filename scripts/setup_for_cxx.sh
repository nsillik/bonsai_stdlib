Delimeter="$RED-----------------------------------------------------------$WHITE"
 Indent="$WHITE           $WHITE-"
    Info="$BLUE   Info    $WHITE-"
Success="$GREEN   Success $WHITE-"
Building="$BLUE   Build   $WHITE-"
  Warn="$YELLOW * Warning $WHITE-"
   Failed="$RED ! Failed  $WHITE-"
    Error="$RED ! Error   $WHITE-"

if [ "$Platform" == "Linux" ] ; then
  PLATFORM_LINKER_OPTIONS="-lpthread -lX11 -ldl -lGL"
  PLATFORM_DEFINES=""
  PLATFORM_DEFINES="$PLATFORM_DEFINES -D BONSAI_LINUX"
  # PLATFORM_DEFINES="$PLATFORM_DEFINES -DBONSAI_SLOW"
  PLATFORM_INCLUDE_DIRS=""
  PLATFORM_CXX_OPTIONS="-ggdb"

  # TODO(Jesse): What does -fPIC acutally do?  I found the option documented,
  # but with no explanation of what it's doing.  Apparently it's unsupported on
  # Windows, so hopefully it's not necessary for anything.
  #
  # Turns out that -fPIC turns on rip-relative addressing (among other things?)
  # such that functions work regardless of where they're loaded into memory.
  # This is important (obviously) for dynamically-loaded libs and ASLR.
  #
  # https://clang.llvm.org/docs/ClangCommandLineReference.html
  # @unsupported_fPIC_flag_windows
  #
  SHARED_LIBRARY_FLAGS="-shared -fPIC"
  PLATFORM_EXE_EXTENSION=""
  PLATFORM_LIB_EXTENSION=".so"

elif [[ "$Platform" == "Windows" ]] ; then

  # advapi32 : tracelogging
  # winmm : timePeriodBegin // timePeriodEnd (scheduling)

  PLATFORM_LINKER_OPTIONS="-lole32 -lAdvapi32 -lWinmm -lgdi32 -luser32 -lopengl32 -lglu32 -W1,/debug,/pdb:name.pdb"


  PLATFORM_DEFINES="-D _CRT_SECURE_NO_WARNINGS -D BONSAI_WIN32"
  PLATFORM_INCLUDE_DIRS=""
  PLATFORM_CXX_OPTIONS="-g -gcodeview"

  # @unsupported_fPIC_flag_windows
  SHARED_LIBRARY_FLAGS="-shared"

  PLATFORM_EXE_EXTENSION=".exe"
  PLATFORM_LIB_EXTENSION=".dll"

elif [[ "$Platform" == "macOS" ]] ; then

  # NOTE(nsillik)(macos): -lGL fails here -- "library 'GL' not found".  OpenGL
  # ships inside the framework.  Cocoa covers AppKit/Foundation.
  PLATFORM_LINKER_OPTIONS="-framework Cocoa -framework OpenGL"

  # NOTE(nsillik)(macos): AppKit's OpenGL surface (NSOpenGL*) has been deprecated
  # since 10.14, every gl* symbol with it, and -[NSApplication
  # activateIgnoringOtherApps:] since 14.0 -- whose replacement, -[NSApplication
  # activate], is 14.0 and so is not reachable from a macos11 deployment target.
  # One flag for the whole class beats a -D plus a #pragma in the source.
  PLATFORM_DEFINES="-D BONSAI_MACOS"

  # NOTE(nsillik)(macos): -target and not -arch.  The SIMD layer is SSE/AVX-only,
  # and -mssse3 -mavx -mavx2 -mfma (in the shared CXX_OPTIONS below, which is not
  # platform-filterable as written) are hard errors for an arm64-apple-darwin
  # target.  So this cross-targets x86_64 and the result runs under Rosetta 2.
  # Phase 4 adds NEON and drops both this flag and those four options.
  #
  # -x objective-c++ because the engine is one translation unit per target and
  # platform/macos/macos_platform.cpp uses AppKit directly; there is no separate
  # .mm shim to put it in.  Objective-C++ is a superset of C++, so the rest of the
  # tree compiles unchanged.
  PLATFORM_CXX_OPTIONS="-ggdb -x objective-c++ -target x86_64-apple-macos11"

  # Cross-targeting makes this fire on /usr/local/include once per target.  It
  # warns that host include directories are unsafe for cross-compilation, which is
  # exactly what is happening on purpose.
  PLATFORM_CXX_OPTIONS="$PLATFORM_CXX_OPTIONS -Wno-poison-system-directories -Wno-deprecated-declarations"

  SHARED_LIBRARY_FLAGS="-shared -fPIC"

  # NOTE(nsillik)(macos): `link_weak` (primitives.h) is `extern "C"
  # __attribute__((weak))` -- the ELF rule for "may be undefined; bind to 0".  ld64 has
  # no equivalent for a main executable: it treats the reference as a strong undefined
  # symbol and fails the link.  weak_import does not help either; it only relaxes a link
  # against a dylib that does define the symbol.
  #
  # -U names each symbol a link is allowed to leave undefined, and it has to *stay*
  # undefined so the dynamic linker can bind it to whatever the loaded game dylib
  # defines -- which is how these hooks are dispatched on every platform.  A weak
  # *definition* here would pin the reference to the engine's own copy instead and
  # silently disable the hook, so it is not an option.
  #
  # Every entry is a link_weak symbol that *some* target leaves undefined, which turns on
  # which translation units that target pulls in rather than on the platform:
  #
  #   BindEngineUniform         defined in src/engine/shader.cpp
  #   LaunchWorkerThreads       defined in bonsai_stdlib/src/work_queue.cpp
  #     ...and src/engine/engine.cpp includes both, so the test binaries -- which do not
  #     include it -- are the links that leave them undefined.
  #   EntityUserData{Serialize,Deserialize,EditorUi}, GameEntityUpdate
  #     game-supplied hooks; the loader and the tools implement none of them.
  #   WorkerThread_BeforeSleep  no definition anywhere; its call site null-checks.
  #
  # Derived by linking with an empty list and reading ld64's undefined-symbol report over
  # the whole target set, not by guessing.  The failure mode of a missing entry is a link
  # error naming the symbol, so the list keeps itself correct.
  #
  # Deliberately not -Wl,-undefined,dynamic_lookup, which disables undefined-symbol
  # checking for the whole link and would turn genuine typos and missing libraries into
  # runtime crashes.
  PLATFORM_LINKER_OPTIONS="$PLATFORM_LINKER_OPTIONS \
    -Wl,-U,_BindEngineUniform \
    -Wl,-U,_EntityUserDataDeserialize \
    -Wl,-U,_EntityUserDataEditorUi \
    -Wl,-U,_EntityUserDataSerialize \
    -Wl,-U,_GameEntityUpdate \
    -Wl,-U,_LaunchWorkerThreads \
    -Wl,-U,_WorkerThread_BeforeSleep"

  PLATFORM_EXE_EXTENSION=""
  PLATFORM_LIB_EXTENSION=".dylib"

  PLATFORM_INCLUDE_DIRS=""

else
  echo "Unsupported Platform ($Platform), exiting." && exit 1
fi

# TODO(Jesse, tags: build_pipeline): Investigate -Wcast-align situation

  # -fsanitize=address

# Note(Jesse): Using -std=c++17 so I can mark functions with [[nodiscard]]

# TODO(Jesse): Figure out how to standardize on a compiler across machines such that
# we can remove -Wno-unknown-warning-optins

# -mvaes

CXX_OPTIONS="
  --std=c++17
  -ferror-limit=2000

  -mssse3

  -mavx
  -mavx2
  -mfma

  -Weverything

  -Wno-reserved-identifier
  -Wno-reserved-id-macro

  -Wno-unknown-warning-option
  -Wno-unsafe-buffer-usage

  -Wno-exit-time-destructors
  -Wno-c++98-compat-pedantic

  -Wno-gnu-anonymous-struct
  -Wno-nested-anon-types

  -Wno-missing-prototypes
  -Wno-zero-as-null-pointer-constant
  -Wno-format-nonliteral
  -Wno-cast-qual
  -Wno-unused-function
  -Wno-four-char-constants
  -Wno-old-style-cast
  -Wno-float-equal
  -Wno-global-constructors
  -Wno-cast-align

  -Wno-switch-enum
  -Wno-switch-default
  -Wno-covered-switch-default

  -Wno-undef
  -Wno-c99-extensions
  -Wno-dollar-in-identifier-extension

  -Wno-class-varargs

  -Wno-unused-value
  -Wno-unused-variable
  -Wno-unused-but-set-variable
  -Wno-unused-parameter

  -Wno-implicit-int-float-conversion
  -Wno-extra-semi-stmt
  -Wno-reorder-init-list
  -Wno-unused-macros

  -Wno-padded
  -Wno-gnu-zero-variadic-macro-arguments

  -Wno-atomic-implicit-seq-cst

  -Wno-nonportable-system-include-path
  -Wno-nonportable-include-path
"

function SetOutputBinaryPathBasename()
{
  base_file="${1##*/}"
  output_basename="$2/${base_file%%.*}"
}

function ColorizeTitle()
{
  echo -e " $YELLOW$1$WHITE"
  echo -e ""
}

declare -a build_job_pids
declare -a build_job_names

# Call like: TrackPid "job name" <pid>
TrackPid() {
    local name=$1
    local pid=$2
    # echo "$name -- $pid"
    build_job_pids=(${build_job_pids[@]} $pid)
    build_job_names=(${build_job_names[@]} $name)
}

WaitForTrackedPids() {
    while [ ${#build_job_pids[@]} -ne 0 ]; do
        # echo "Waiting for pids: ${build_job_pids[@]}"
        local range=$(eval echo {0..$((${#build_job_pids[@]}-1))})
        local i
        for i in $range; do
            if ! kill -0 ${build_job_pids[$i]} 2> /dev/null; then

                exit_code=0
                wait ${build_job_pids[$i]} || exit_code=$?

                if [ $exit_code -eq 0 ]; then
                  echo -e "$Success ${build_job_names[$i]}"
                else
                  echo -e "$Failed ${build_job_names[$i]}"
                  exit 1
                fi

                unset build_job_pids[$i]
                unset build_job_names[$i]
            fi
        done
         # Expunge nulls created by unset.
        build_job_pids=("${build_job_pids[@]}")
        build_job_names=("${build_job_names[@]}")
        sleep 0.25
    done
}

