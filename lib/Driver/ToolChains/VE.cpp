//===--- VE.cpp - VE ToolChain Implementations ------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "VE.h"
#include "CommonArgs.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/Driver.h"
#include "clang/Driver/Options.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include <cstdlib> // ::getenv

using namespace clang::driver;
using namespace clang::driver::toolchains;
using namespace clang;
using namespace llvm::opt;

/// VE Tools
// We pass assemble and link construction to the ncc tool.

void tools::VE::Assembler::ConstructJob(Compilation &C, const JobAction &JA,
                                        const InputInfo &Output,
                                        const InputInfoList &Inputs,
                                        const ArgList &Args,
                                        const char *LinkingOutput) const {
  claimNoWarnArgs(Args);
  ArgStringList CmdArgs;

  CmdArgs.push_back("-o");
  CmdArgs.push_back(Output.getFilename());

  CmdArgs.push_back("-c");

  if (Args.hasArg(options::OPT_v))
    CmdArgs.push_back("-v");

  if (Arg *A = Args.getLastArg(options::OPT_g_Group))
    if (!A->getOption().matches(options::OPT_g0))
      CmdArgs.push_back("-g");

  if (Args.hasFlag(options::OPT_fverbose_asm, options::OPT_fno_verbose_asm,
                   false))
    CmdArgs.push_back("-fverbose-asm");

  Args.AddAllArgValues(CmdArgs, options::OPT_Wa_COMMA, options::OPT_Xassembler);

  for (const auto &II : Inputs)
    CmdArgs.push_back(II.getFilename());

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("ncc"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

void tools::VE::Linker::ConstructJob(Compilation &C, const JobAction &JA,
                                     const InputInfo &Output,
                                     const InputInfoList &Inputs,
                                     const ArgList &Args,
                                     const char *LinkingOutput) const {
  ArgStringList CmdArgs;

  bool IsStatic = Args.hasArg(options::OPT_static);
  bool IsShared = Args.hasArg(options::OPT_shared);

  if (IsShared)
    CmdArgs.push_back("-shared");

  if (IsStatic)
    CmdArgs.push_back("-static");

  if (Output.isFilename()) {
    CmdArgs.push_back("-o");
    CmdArgs.push_back(Output.getFilename());
  } else {
    assert(Output.isNothing() && "Invalid output.");
  }

  if (Args.hasArg(options::OPT_v))
    CmdArgs.push_back("-v");

  // Pass -fexceptions through to the linker if it was present.
  if (Args.hasFlag(options::OPT_fexceptions, options::OPT_fno_exceptions,
                   false))
    CmdArgs.push_back("-fexceptions");

  Args.AddAllArgs(CmdArgs, options::OPT_L);
  getToolChain().AddFilePathLibArgs(Args, CmdArgs);
  AddLinkerInputs(getToolChain(), Inputs, Args, CmdArgs, JA);

  if (!Args.hasArg(options::OPT_nostdlib)) {
    if (!Args.hasArg(options::OPT_nodefaultlibs)) {
      AddRunTimeLibs(getToolChain(), getToolChain().getDriver(), CmdArgs, Args);
    }
  }

  const char *Exec = Args.MakeArgString(getToolChain().GetProgramPath("ncc"));
  C.addCommand(llvm::make_unique<Command>(JA, *this, Exec, CmdArgs, Inputs));
}

/// VE tool chain
VEToolChain::VEToolChain(const Driver &D, const llvm::Triple &Triple,
                         const ArgList &Args)
    : ToolChain(D, Triple, Args) {
  getProgramPaths().push_back("/opt/nec/ve/bin");
  // ProgramPaths are found via 'PATH' environment variable.
}

Tool *VEToolChain::buildAssembler() const {
  return new tools::VE::Assembler(*this);
}

Tool *VEToolChain::buildLinker() const {
  return new tools::VE::Linker(*this);
}

bool VEToolChain::isPICDefault() const { return false; }

bool VEToolChain::isPIEDefault() const { return false; }

bool VEToolChain::isPICDefaultForced() const { return false; }

bool VEToolChain::SupportsProfiling() const { return false; }

bool VEToolChain::hasBlocksRuntime() const { return false; }

void VEToolChain::AddClangSystemIncludeArgs(const ArgList &DriverArgs,
                                               ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(clang::driver::options::OPT_nostdinc))
    return;

  if (DriverArgs.hasArg(options::OPT_nobuiltininc) &&
      DriverArgs.hasArg(options::OPT_nostdlibinc))
    return;

  if (!DriverArgs.hasArg(options::OPT_nostdlibinc)) {
    if (const char *cl_include_dir = getenv("NCC_C_INCLUDE_PATH")) {
      SmallVector<StringRef, 4> Dirs;
      const char EnvPathSeparatorStr[] = {llvm::sys::EnvPathSeparator, '\0'};
      StringRef(cl_include_dir).split(Dirs, StringRef(EnvPathSeparatorStr));
      ArrayRef<StringRef> DirVec(Dirs);
      addSystemIncludes(DriverArgs, CC1Args, DirVec);
    } else {
      addSystemInclude(DriverArgs, CC1Args,
                       getDriver().SysRoot + "/opt/nec/ve/musl/include");
    }
#if 0
    // FIXME: Remedy for /opt/nec/ve/musl/include/bits/alltypes.h
    CC1Args.push_back("-D__NEED_off_t");
#endif
  }

  if (!DriverArgs.hasArg(options::OPT_nobuiltininc)) {
    SmallString<128> P(getDriver().ResourceDir);
    llvm::sys::path::append(P, "include");
    addSystemInclude(DriverArgs, CC1Args, P);
  }
}

void VEToolChain::addClangTargetOptions(const ArgList &DriverArgs,
                                           ArgStringList &CC1Args,
                                           Action::OffloadKind) const {
  CC1Args.push_back("-nostdsysteminc");
  CC1Args.push_back("-fuse-init-array");
}

void VEToolChain::AddClangCXXStdlibIncludeArgs(
    const ArgList &DriverArgs, ArgStringList &CC1Args) const {
  if (DriverArgs.hasArg(clang::driver::options::OPT_nostdinc) ||
      DriverArgs.hasArg(options::OPT_nostdlibinc) ||
      DriverArgs.hasArg(options::OPT_nostdincxx))
    return;
  if (const char *cl_include_dir = getenv("NCC_CPLUS_INCLUDE_PATH")) {
    SmallVector<StringRef, 4> Dirs;
    const char EnvPathSeparatorStr[] = {llvm::sys::EnvPathSeparator, '\0'};
    StringRef(cl_include_dir).split(Dirs, StringRef(EnvPathSeparatorStr));
    ArrayRef<StringRef> DirVec(Dirs);
    addSystemIncludes(DriverArgs, CC1Args, DirVec);
  } else {
#if 0
    // Use default include paths
    addSystemInclude(DriverArgs, CC1Args,
                     getDriver().SysRoot + "/opt/nec/ve/ncc/1.3.3/include/C++");
#endif
  }
#if 0
  // Several remedies are required to use VE system header files.
  // FIXME: Remedy for /opt/nec/ve/ncc/1.3.3/include/necvals.h problem.
  CC1Args.push_back("-D_CHAR16T");
  // FIXME: Remedy for /opt/nec/ve/ncc/1.3.3/include/C++/limits
  CC1Args.push_back("-D__INFINITY__=INFINITY");
#endif
}

void VEToolChain::AddCXXStdlibLibArgs(const ArgList &Args,
                                         ArgStringList &CmdArgs) const {
  // We don't output any lib args. This is handled by ncc.
}
