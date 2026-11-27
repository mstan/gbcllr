Pinned Boost.Context 1.90.0 System V AMD64 ELF and MS x64 PE make/jump assembly.
The ELF files are unmodified. The PE files omit only upstream DLL-export
directives because CMake gives these private functions project-specific names.
UPSTREAM.json records exact URLs and hashes; LICENSE_1_0.txt is retained.
CMake prefixes the two symbols through assembler preprocessor definitions.

Linux x86-64 LP64 glibc builds can select the SysV experimental backend.
The assembly preserves SysV callee-saved registers, MXCSR and the x87 control
word. Caller-saved registers, signal masks and TLS remain owner-thread state;
contexts never migrate between threads. The baseline AMD64 instruction set
includes SSE2; no optional TSX path is selected. Stacks use normal writable
memory and the assembly carries a non-executable GNU-stack note.

The adapter declares the System V ABI explicitly. make takes the stack's upper
address, its byte size and an entry function, returning a context handle inside
that stack. jump takes a destination handle and a data pointer; the destination
receives the suspended caller's handle and that pointer in a two-pointer
transfer value. The make routine aligns the stack to 16 bytes and establishes
the ABI entry alignment. The register-frame diagrams in the pinned assembly
specify its offsets. R12–R15, RBX, RBP, RSP and the return continuation are saved;
integer/vector caller-saved registers and flags may change. The x87 register
stack follows the ABI's call boundary rules; MXCSR and the x87 control word
are preserved. Context storage stays alive until the idle loop has terminated.
C++ exceptions are caught in that loop and rethrown on the owning caller stack.

At context construction, ARCH_SHSTK_STATUS checks the actual thread state.
Active shadow stacks or an unknown result select glibc ucontext instead;
unavailable headers also select that fallback. No shadow-stack allocation or
optional shadow-stack instruction is needed in the fast profile. ASan switch
annotations cover both paths. GBCLLR_PORTABLE_CABLE overrides both with the
universal thread implementation; unsupported targets keep their existing path.
The actual selected backend is reported by the paired runner.

Windows x64 MinGW GNU/Clang builds can select the PE implementation behind the
same OFF-by-default flag. Its explicit MS ABI preserves R12-R15, RDI, RSI, RBX,
RBP, RSP, XMM6-XMM15, MXCSR and the x87 control word; ordinary volatile registers
and flags may change. Upstream also saves/restores the NT_TIB stack bounds,
deallocation stack and fiber storage. The adapter owns a fixed 1 MiB writable
stack and uses the same exception and idle-loop lifetime contract. No TSX path
is selected. The contract test keeps nonvolatile integer/vector registers live
across real context switches and checks a 64 KiB suspended local array.

The Windows adapter queries ProcessUserShadowStackPolicy once at construction,
preserving GetLastError. Failed/unknown queries, ASan and every flag except
CetDynamicApisOutOfProcOnly retain the existing OS fiber implementation. That
documented flag restricts two dynamic-code registration APIs the static assembly
never calls; it does not enable shadow stacks. No mitigation is changed. See
[Microsoft's policy definition](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-process_mitigation_user_shadow_stack_policy).
Active shadow stacks therefore always select OS fibers. Portable thread fallback
remains available. The separately opted-in v2 mixed-ABI namespace includes this
Windows path after bounded actual Gold/Silver paired state/PCM/save and disrupted
PC/Deck network qualification. This does not establish general title or sustained
full-speed acceptance; exact packages and limits are in
[the evidence](../../docs/evidence/2026-11-03-windows-context.json).
