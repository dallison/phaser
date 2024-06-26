"""
This module provides a rule to generate phaser message files from proto_library targets.
"""

load("@bazel_skylib//lib:paths.bzl", "paths")

MessageInfo = provider(fields = ["direct_sources", "transitive_sources", "cpp_outputs"])

def _phaser_action(
        ctx,
        direct_sources,
        transitive_sources,
        out_dir,
        package_name,
        outputs,
        add_namespace,
        target_name):
    # The protobuf compiler allow plugins to get arguments specified in the --plugin_out
    # argument.  The args are passed as a comma separated list of key=value pairs followed
    # by a colon and the output directory.
    options_and_out_dir = ""
    if add_namespace != "":
        options_and_out_dir = "--phaser_out=add_namespace={},package_name={},target_name={}:{}".format(add_namespace, package_name, target_name, out_dir)
    else:
        options_and_out_dir = "--phaser_out=package_name={},target_name={}:{}".format(package_name, target_name, out_dir)

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
def _phaser_aspect_impl(target, _ctx):
    direct_sources = []
    transitive_sources = depset()
    cpp_outputs = []

    def add_output(base):
        cpp_outputs.append(paths.replace_extension(base, ".phaser.cc"))
        cpp_outputs.append(paths.replace_extension(base, ".phaser.h"))

    if ProtoInfo in target:
        transitive_sources = target[ProtoInfo].transitive_sources
        for s in transitive_sources.to_list():
            direct_sources.append(s)
            file_path = s.short_path
            if "_virtual_imports" in file_path:
                # For a file that is not in this package, we need to generate the
                # output in our package.
                # The path looks like:
                # ../com_google_protobuf/_virtual_imports/any_proto/google/protobuf/any.proto
                # We want to declare the file as:ƒ
                # google/protobuf/any.phaser.cc
                v = file_path.split("_virtual_imports/")

                # Remove the first directory of v[1] to get the path relative to the package.
                file_path = v[1].split("/", 1)[1]
            add_output(file_path)

    return [MessageInfo(
        direct_sources = direct_sources,
        transitive_sources = transitive_sources,
        cpp_outputs = cpp_outputs,
    )]

phaser_aspect = aspect(
    attr_aspects = ["deps"],
    provides = [MessageInfo],
    implementation = _phaser_aspect_impl,
)

# The phaser rule runs the Phaser plugin from the protoc compiler.
# The deps for the rule are proto_libraries that contain the protobuf files.
def _phaser_impl(ctx):
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
            if out_file.extension == "h":
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
        "package_name": attr.string(),
        "target_name": attr.string(),
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

def phaser_library(name, deps = [], runtime = "@phaser//phaser/runtime:phaser_runtime", add_namespace = ""):
    """
    Generate a cc_libary for protobuf files specified in deps.

    Args:
        name: name
        deps: proto_libraries that contain the protobuf files
        deps: dependencies
        runtime: label for phaser runtime.
        add_namespace: add given namespace to the message output
    """
    phaser = name + "_phaser"

    _phaser_gen(
        name = phaser,
        deps = deps,
        add_namespace = add_namespace,
        package_name = native.package_name(),
        target_name = name,
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

    native.cc_library(
        name = name,
        srcs = [srcs],
        hdrs = [hdrs],
        deps = libdeps,
    )
