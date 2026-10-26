"""
This module provides a rule to generate phaser message files from proto_library targets.
"""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")
load("@pybind11_bazel//:build_defs.bzl", "pybind_extension")
load("@rules_cc//cc:defs.bzl", "cc_library")

MessageInfo = provider(fields = [
    "direct_sources",
    "transitive_sources",
    "cpp_outputs",
    "symlink_headers",
    "python_registers",
])

def _python_register_symbol(base):
    stem = base[:-len(".proto")] if base.endswith(".proto") else base
    chars = []
    for i in range(len(stem)):
        c = stem[i]
        chars.append(c if c.isalnum() else "_")
    return "phaser_python_register_" + "".join(chars)

def _sanitize_identifier(name):
    chars = []
    for i in range(len(name)):
        c = name[i]
        if c.isalnum() or c == "_":
            chars.append(c)
        else:
            chars.append("_")
    if not chars or chars[0].isdigit():
        chars.insert(0, "_")
    return "".join(chars)

def _python_module_source(module_name, registers):
    lines = [
        "#include <pybind11/pybind11.h>",
        "namespace py = pybind11;",
        "namespace phaser::python {",
    ]
    for sym in registers:
        lines.append("void {}(py::module_&);".format(sym))
    lines.append("}  // namespace phaser::python")
    lines.append("PYBIND11_MODULE({}, m) {{".format(module_name))
    for sym in registers:
        lines.append("  phaser::python::{}(m);".format(sym))
    lines.append("}")
    lines.append("")
    return "\n".join(lines)

def _phaser_action(
        ctx,
        direct_sources,
        transitive_sources,
        out_dir,
        package_name,
        outputs,
        add_namespace,
        target_name,
        frontend,
        enable_active_message,
        ros_metadata):
    # The protobuf compiler allow plugins to get arguments specified in the --plugin_out
    # argument.  The args are passed as a comma separated list of key=value pairs followed
    # by a colon and the output directory.
    options = []
    if add_namespace != "":
        options.append("add_namespace={}".format(add_namespace))
    options.append("package_name={}".format(package_name))
    options.append("target_name={}".format(target_name))
    options.append("frontend={}".format(frontend))
    if enable_active_message:
        options.append("active_message=true")
    if ros_metadata:
        options.append("ros_metadata=true")
    options_and_out_dir = "--phaser_out={}:{}".format(",".join(options), out_dir)

    inputs = depset(direct = direct_sources, transitive = transitive_sources)

    import_paths = {}
    for s in transitive_sources:
        for f in s.to_list():
            if f.is_source and f.short_path.startswith("../"):
                # External repository sources have short paths of the form
                # ../<canonical_repo_name>/<proto_import_path>. Add that
                # repository's execroot directory so imports retain their
                # canonical proto paths.
                repository = f.short_path.split("/", 2)[1]
                import_paths["-Iexternal/" + repository] = None
            if not f.is_source:
                index = f.path.find("_virtual_imports")
                if index != -1:
                    # Go to first slash after _virtual_imports/
                    slash = f.path.find("/", index + 17)
                    import_paths["-I" + f.path[:slash]] = None

    plugin, _, plugin_manifests = ctx.resolve_command(tools = [ctx.attr.phaser_plugin])
    plugin_arg = "--plugin=protoc-gen-phaser={}".format(ctx.executable.phaser_plugin.path)

    args = ctx.actions.args()
    args.add(plugin_arg)
    args.add(options_and_out_dir)
    args.add_all(inputs)
    args.add_all(import_paths.keys())
    args.add("-I.")

    ctx.actions.run(
        inputs = inputs,
        tools = plugin,
        input_manifests = plugin_manifests,
        executable = ctx.executable.protoc,
        outputs = outputs,
        arguments = [args],
        progress_message = "Generating phaser message files %s" % ctx.label,
        mnemonic = "Phaser",
    )

# This aspect generates the MessageInfo provider containing the files we
# will generate from running the Phaser plugin.
def _to_list(value):
    if type(value) == "list":
        return value
    return value.to_list()

def _proto_output_base(source_file):
    file_path = source_file.short_path
    if file_path.startswith("../"):
        # Strip Bazel's ../<repo>/ prefix for external-repository sources.
        # Otherwise it escapes target_name and collides across frontends.
        file_path = file_path.split("/", 2)[2]
    if "_virtual_imports" in file_path:
        # For a file that is not in this package, we need to generate the
        # output in our package.
        # The path looks like:
        # ../com_google_protobuf/_virtual_imports/any_proto/google/protobuf/any.proto
        # We want to declare the file as:
        # google/protobuf/any.phaser.cc
        v = file_path.split("_virtual_imports/")

        # Remove the first directory of v[1] to get the path relative to the package.
        file_path = v[1].split("/", 1)[1]
    return file_path

def _skip_phaser_generation(source_file):
    base = _proto_output_base(source_file)
    if base.endswith("google/protobuf/descriptor.proto"):
        return True
    if base.endswith("phaser/options.proto"):
        return True
    return False

def _phaser_aspect_impl(target, ctx):
    direct_sources = []
    transitive_sources = depset()
    cpp_outputs = []
    symlink_headers = []
    python_registers = []

    def add_output(base, symlink):
        cpp_outputs.append(paths.replace_extension(base, ".phaser.cc"))
        cpp_outputs.append(paths.replace_extension(base, ".pybind.cc"))
        header = paths.replace_extension(base, ".phaser.h")
        cpp_outputs.append(header)
        if symlink:
            symlink_headers.append(header)

    if ProtoInfo in target:
        transitive_sources = target[ProtoInfo].transitive_sources
        direct_paths = {s.path: True for s in _to_list(target[ProtoInfo].direct_sources)}
        source_bases = []
        for s in _to_list(transitive_sources):
            if _skip_phaser_generation(s):
                continue
            direct_sources.append(s)
            base = _proto_output_base(s)
            source_bases.append(base)
            add_output(base, s.path in direct_paths)

        # Dependency protos first, then anything else this target imports, then
        # this target's own files. Each register function binds its classes
        # before its methods, so a later file can return an earlier type.
        seen = {}

        def add_symbol(sym):
            if sym in seen:
                return
            seen[sym] = True
            python_registers.append(sym)

        if hasattr(ctx.rule.attr, "deps"):
            for dep in ctx.rule.attr.deps:
                if MessageInfo in dep:
                    for sym in dep[MessageInfo].python_registers:
                        add_symbol(sym)
        direct_syms = []
        for s in _to_list(target[ProtoInfo].direct_sources):
            if _skip_phaser_generation(s):
                continue
            direct_syms.append(_python_register_symbol(_proto_output_base(s)))
        for base in source_bases:
            sym = _python_register_symbol(base)
            if sym not in direct_syms:
                add_symbol(sym)
        for sym in direct_syms:
            add_symbol(sym)

    return [MessageInfo(
        direct_sources = direct_sources,
        transitive_sources = transitive_sources,
        cpp_outputs = cpp_outputs,
        symlink_headers = symlink_headers,
        python_registers = python_registers,
    )]

phaser_aspect = aspect(
    attr_aspects = ["deps"],
    provides = [MessageInfo],
    implementation = _phaser_aspect_impl,
)

# The phaser rule runs the Phaser plugin from the protoc compiler.
# The deps for the rule are proto_libraries that contain the protobuf files.
def _phaser_impl(ctx):
    frontend = ctx.attr.frontend
    if frontend not in ("protobuf", "ros"):
        fail("phaser_library frontend must be 'protobuf' or 'ros', got: {}".format(frontend))

    outputs = []

    direct_sources = []
    transitive_sources = []
    cpp_outputs = []
    python_registers = []
    seen_registers = {}
    package_name = ctx.attr.package_name
    for dep in ctx.attr.deps:
        dep_outs = []
        for out in dep[MessageInfo].cpp_outputs:
            out_name = ctx.attr.target_name + "/" + out
            out_file = ctx.actions.declare_file(out_name)
            dep_outs.append(out_file)

            # If we are creating a header file in our package, we need to create a symlink to it.
            # This is because the header file will be something like
            # phaser/testdata/phaser/testdata/Test.phaser.h
            # but we want to be able to do:
            # #include "phaser/testdata/Test.phaser.h"
            # so we create the symlink:
            # Test.phaser.h -> phaser/testdata/phaser/testdata/Test.phaser.h
            if (
                ctx.attr.direct_header_symlinks and
                out_file.extension == "h" and
                out in dep[MessageInfo].symlink_headers
            ):
                package_prefix = ctx.label.package + "/"
                symlink_name = out
                if not ctx.label.package or symlink_name.startswith(package_prefix):
                    # Header is in our package, remove the package name. If
                    # the header is outside our package (like
                    # google/protobuf/any.h), it is already in the right place.
                    if package_prefix:
                        symlink_name = symlink_name[len(package_prefix):]
                    symlink = ctx.actions.declare_file(symlink_name)
                    ctx.actions.symlink(output = symlink, target_file = out_file)
                    dep_outs.append(symlink)
            cpp_outputs.append(out_file)

        direct_sources += dep[MessageInfo].direct_sources
        transitive_sources.append(dep[MessageInfo].transitive_sources)
        for sym in dep[MessageInfo].python_registers:
            if sym not in seen_registers:
                seen_registers[sym] = True
                python_registers.append(sym)
        outputs += dep_outs

    _phaser_action(
        ctx,
        direct_sources,
        transitive_sources,
        ctx.bin_dir.path,
        ctx.attr.package_name,
        cpp_outputs,
        ctx.attr.add_namespace,
        ctx.attr.target_name,
        frontend,
        ctx.attr.enable_active_message,
        ctx.attr.ros_metadata,
    )

    if ctx.attr.python:
        if frontend != "protobuf":
            fail("phaser_library python bindings require frontend=protobuf")
        module_name = _sanitize_identifier(ctx.attr.target_name + "_py")
        umbrella = ctx.actions.declare_file(module_name + "_module.pybind.cc")
        ctx.actions.write(
            output = umbrella,
            content = _python_module_source(module_name, python_registers),
        )
        outputs.append(umbrella)

    return [DefaultInfo(files = depset(outputs))]

_phaser_gen = rule(
    attrs = {
        "protoc": attr.label(
            executable = True,
            default = Label("@com_google_protobuf//:protoc"),
            cfg = "exec",
        ),
        "phaser_plugin": attr.label(
            executable = True,
            default = Label("//phaser/compiler:phaser"),
            cfg = "exec",
        ),
        "deps": attr.label_list(
            aspects = [phaser_aspect],
        ),
        "add_namespace": attr.string(),
        "direct_header_symlinks": attr.bool(default = True),
        "package_name": attr.string(),
        "target_name": attr.string(),
        "frontend": attr.string(default = "protobuf"),
        "enable_active_message": attr.bool(default = False),
        "ros_metadata": attr.bool(default = False),
        "python": attr.bool(default = False),
    },
    implementation = _phaser_impl,
)

def _split_files_impl(ctx):
    files = []
    for file in ctx.files.deps:
        if file.extension != ctx.attr.ext:
            continue
        if ctx.attr.exclude_suffix and file.basename.endswith(ctx.attr.exclude_suffix):
            continue
        if ctx.attr.include_suffix and not file.basename.endswith(ctx.attr.include_suffix):
            continue
        files.append(file)

    return [DefaultInfo(files = depset(files))]

_split_files = rule(
    attrs = {
        "deps": attr.label_list(mandatory = True),
        "ext": attr.string(mandatory = True),
        "exclude_suffix": attr.string(default = ""),
        "include_suffix": attr.string(default = ""),
    },
    implementation = _split_files_impl,
)

def phaser_library(
        name,
        deps = [],
        runtime = "@phaser//phaser/runtime:phaser_runtime",
        add_namespace = "",
        enable_active_message = False,
        frontend = "protobuf",
        cc_deps = [],
        copts = [],
        direct_header_symlinks = True,
        ros_metadata = False,
        python = False):
    """
    Generate a cc_libary for protobuf files specified in deps.

    Args:
        name: name
        deps: proto_libraries that contain the protobuf files
        deps: dependencies
        runtime: label for phaser runtime.
        add_namespace: add given namespace to the message output
        enable_active_message: if True, generated message types get a public
            `phaser::ActiveMessage active_message` field, which holds a
            `std::any` (also enableable via the
            `active_message=true` plugin command-line option).
        frontend: generated C++ API style, either "protobuf" (default) or "ros".
        cc_deps: additional C++ dependencies required by generated headers,
            such as ROS1 message/runtime libraries for intrinsic ROS fields.
        copts: additional C++ compiler options for generated sources.
        direct_header_symlinks: create short direct-source header aliases.
            Disable this when generating multiple frontends from one proto target.
        ros_metadata: generate ROS datatype, definition, and MD5 functions.
        python: also build a pybind11 module named `<name>_py`. Messages are
            exposed with the protobuf getter and setter API (`x`, `set_x`,
            `has_x`, `clear_x`, and the repeated `add_`/`size` methods). The
            ROS frontend is not supported.
    """
    if frontend not in ("protobuf", "ros"):
        fail("phaser_library frontend must be 'protobuf' or 'ros', got: {}".format(frontend))
    if python and frontend != "protobuf":
        fail("phaser_library python bindings require frontend=protobuf")

    output_package_name = native.package_name()
    repository_name = native.repository_name()
    if repository_name.startswith("@@"):
        repository_name = repository_name[2:]
    elif repository_name.startswith("@"):
        repository_name = repository_name[1:]
    if repository_name:
        output_package_name = paths.join("external", repository_name, output_package_name)

    phaser = name + "_phaser"
    _phaser_gen(
        name = phaser,
        deps = deps,
        add_namespace = add_namespace,
        direct_header_symlinks = direct_header_symlinks,
        package_name = output_package_name,
        target_name = name,
        enable_active_message = enable_active_message,
        frontend = frontend,
        ros_metadata = ros_metadata,
        python = python,
    )

    srcs = name + "_srcs"
    _split_files(
        name = srcs,
        ext = "cc",
        exclude_suffix = ".pybind.cc",
        deps = [phaser],
    )

    hdrs = name + "_hdrs"
    _split_files(
        name = hdrs,
        ext = "h",
        deps = [phaser],
    )

    libdeps = []
    for dep in deps:
        if not dep.endswith("_proto"):
            libdeps.append(dep)

    if runtime != "":
        libdeps = libdeps + [runtime]
    # Generated sources register embedded FileDescriptorProtos with this
    # library. The descriptor is then available without linking the .pb.cc.
    libdeps.append(Label("//phaser/runtime:phaser_bank_protobuf"))
    libdeps = libdeps + cc_deps

    # PhaserBank entries are registered by static initializers in the generated
    # sources. alwayslink keeps those objects when a binary depends on the
    # library but only calls the bank by message name.
    cc_library(
        name = name,
        alwayslink = True,
        copts = copts,
        srcs = [srcs],
        hdrs = [hdrs],
        deps = libdeps,
    )

    if python:
        py_name = _sanitize_identifier(name + "_py")
        py_srcs = name + "_py_srcs"
        _split_files(
            name = py_srcs,
            ext = "cc",
            include_suffix = ".pybind.cc",
            deps = [phaser],
        )
        pybind_extension(
            name = py_name,
            srcs = [py_srcs],
            deps = [name],
        )
