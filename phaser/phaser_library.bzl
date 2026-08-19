"""
This module provides a rule to generate phaser message files from proto_library targets.
"""

load("@bazel_skylib//lib:paths.bzl", "paths")
load("@com_google_protobuf//bazel/common:proto_info.bzl", "ProtoInfo")
load("@rules_cc//cc:defs.bzl", "cc_library")

MessageInfo = provider(fields = [
    "direct_sources",
    "transitive_sources",
    "cpp_outputs",
    "symlink_headers",
])

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
        enable_active_message):
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
    options_and_out_dir = "--phaser_out={}:{}".format(",".join(options), out_dir)

    inputs = depset(direct = direct_sources, transitive = transitive_sources)

    import_paths = []
    for s in transitive_sources:
        for f in s.to_list():
            if not f.is_source:
                index = f.path.find("_virtual_imports")
                if index != -1:
                    # Go to first slash after _virtual_imports/
                    slash = f.path.find("/", index + 17)
                    import_paths.append("-I" + f.path[:slash])

    plugin, _, plugin_manifests = ctx.resolve_command(tools = [ctx.attr.phaser_plugin])
    plugin_arg = "--plugin=protoc-gen-phaser={}".format(ctx.executable.phaser_plugin.path)

    args = ctx.actions.args()
    args.add(plugin_arg)
    args.add(options_and_out_dir)
    args.add_all(inputs)
    args.add_all(import_paths)
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
    if base == "google/protobuf/descriptor.proto":
        return True
    if base == "phaser/options.proto":
        return True
    return False

def _phaser_aspect_impl(target, _ctx):
    direct_sources = []
    transitive_sources = depset()
    cpp_outputs = []
    symlink_headers = []

    def add_output(base, symlink):
        cpp_outputs.append(paths.replace_extension(base, ".phaser.cc"))
        header = paths.replace_extension(base, ".phaser.h")
        cpp_outputs.append(header)
        if symlink:
            symlink_headers.append(header)

    if ProtoInfo in target:
        transitive_sources = target[ProtoInfo].transitive_sources
        direct_paths = {s.path: True for s in _to_list(target[ProtoInfo].direct_sources)}
        for s in _to_list(transitive_sources):
            if _skip_phaser_generation(s):
                continue
            direct_sources.append(s)
            add_output(_proto_output_base(s), s.path in direct_paths)

    return [MessageInfo(
        direct_sources = direct_sources,
        transitive_sources = transitive_sources,
        cpp_outputs = cpp_outputs,
        symlink_headers = symlink_headers,
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
                prefix = paths.join(ctx.attr.target_name, package_name)
                symlink_name = out_file.short_path[len(prefix) + 1:]
                if symlink_name.startswith(package_name):
                    # Header is in our package, remove the package name.
                    # If the header is outside our package (like google/protobuf/any.h),
                    # we don't want to create a symlink to it becuase it's in
                    # the right place already.
                    symlink_name = symlink_name[len(package_name) + 1:]
                    symlink = ctx.actions.declare_file(symlink_name)
                    ctx.actions.symlink(output = symlink, target_file = out_file)
                    dep_outs.append(symlink)
            cpp_outputs.append(out_file)

        direct_sources += dep[MessageInfo].direct_sources
        transitive_sources.append(dep[MessageInfo].transitive_sources)
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
    )

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
    },
    implementation = _phaser_impl,
)

def _split_files_impl(ctx):
    files = []
    for file in ctx.files.deps:
        if file.extension == ctx.attr.ext:
            files.append(file)

    return [DefaultInfo(files = depset(files))]

_split_files = rule(
    attrs = {
        "deps": attr.label_list(mandatory = True),
        "ext": attr.string(mandatory = True),
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
        direct_header_symlinks = True):
    """
    Generate a cc_libary for protobuf files specified in deps.

    Args:
        name: name
        deps: proto_libraries that contain the protobuf files
        deps: dependencies
        runtime: label for phaser runtime.
        add_namespace: add given namespace to the message output
        enable_active_message: if True, generated message types get a public
            `std::any active_message` field (also enableable via the
            `active_message=true` plugin command-line option).
        frontend: generated C++ API style, either "protobuf" (default) or "ros".
        cc_deps: additional C++ dependencies required by generated headers,
            such as ROS1 message/runtime libraries for intrinsic ROS fields.
        direct_header_symlinks: create short direct-source header aliases.
            Disable this when generating multiple frontends from one proto target.
    """
    if frontend not in ("protobuf", "ros"):
        fail("phaser_library frontend must be 'protobuf' or 'ros', got: {}".format(frontend))

    phaser = name + "_phaser"

    _phaser_gen(
        name = phaser,
        deps = deps,
        add_namespace = add_namespace,
        direct_header_symlinks = direct_header_symlinks,
        package_name = native.package_name(),
        target_name = name,
        enable_active_message = enable_active_message,
        frontend = frontend,
    )

    srcs = name + "_srcs"
    _split_files(
        name = srcs,
        ext = "cc",
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
    libdeps = libdeps + cc_deps

    cc_library(
        name = name,
        srcs = [srcs],
        hdrs = [hdrs],
        deps = libdeps,
    )
