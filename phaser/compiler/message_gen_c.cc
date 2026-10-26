// Copyright 2024-2026 David Allison
// All Rights Reserved
// See LICENSE file for licensing information.

#include "phaser/compiler/gen.h"

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_replace.h"

namespace phaser {
namespace {

bool IsRustKeyword(std::string_view name) {
  static constexpr std::string_view kWords[] = {
      "Self",    "abstract", "as",       "async",    "await",   "become",
      "box",     "break",    "const",    "continue", "crate",   "do",
      "dyn",     "else",     "enum",     "extern",   "false",   "final",
      "fn",      "for",      "if",       "impl",     "in",      "let",
      "loop",    "macro",    "match",    "mod",      "move",    "mut",
      "override","priv",     "pub",      "ref",      "return",  "self",
      "static",  "struct",   "super",    "trait",    "true",    "try",
      "type",    "typeof",   "union",    "unsafe",   "unsized", "use",
      "virtual", "where",    "while",    "yield",
  };
  for (std::string_view word : kWords) {
    if (name == word) {
      return true;
    }
  }
  return false;
}

std::string RustIdent(std::string name) {
  while (IsRustKeyword(name)) {
    name.push_back('_');
  }
  return name;
}

std::string SanitizeIdent(std::string name) {
  for (char& c : name) {
    const unsigned char uc = static_cast<unsigned char>(c);
    const bool ok = (uc >= '0' && uc <= '9') || (uc >= 'A' && uc <= 'Z') ||
                    (uc >= 'a' && uc <= 'z') || c == '_';
    if (!ok) {
      c = '_';
    }
  }
  if (name.empty() || (name[0] >= '0' && name[0] <= '9')) {
    name.insert(name.begin(), '_');
  }
  return name;
}

std::string FlatMessageName(const google::protobuf::Descriptor* desc) {
  if (desc->containing_type() == nullptr) {
    return std::string(desc->name());
  }
  return std::string(desc->containing_type()->name()) + "_" +
         std::string(desc->name());
}

std::string FlatEnumName(const google::protobuf::EnumDescriptor* desc) {
  if (desc->containing_type() == nullptr) {
    return std::string(desc->name());
  }
  return std::string(desc->containing_type()->name()) + "_" +
         std::string(desc->name());
}

std::string CPrefixFrom(const std::string& package, const std::string& flat) {
  if (package.empty()) {
    return "phaser_" + SanitizeIdent(flat);
  }
  return "phaser_" + SanitizeIdent(package) + "_" + SanitizeIdent(flat);
}

std::string CMessagePrefix(const google::protobuf::Descriptor* desc) {
  return CPrefixFrom(std::string(desc->file()->package()), FlatMessageName(desc));
}

std::string CEnumPrefix(const google::protobuf::EnumDescriptor* desc) {
  return CPrefixFrom(std::string(desc->file()->package()), FlatEnumName(desc));
}

void EnsureType(std::ostream& decl, std::vector<std::string>* declared,
                const std::string& prefix) {
  if (prefix == "phaser_AnyMessage") {
    return;
  }
  for (const std::string& name : *declared) {
    if (name == prefix) {
      return;
    }
  }
  declared->push_back(prefix);
  decl << "typedef struct " << prefix << " " << prefix << ";\n";
}

void EmitEnum(const google::protobuf::EnumDescriptor* desc, std::ostream& decl,
              std::ostream* rust) {
  const std::string prefix = CEnumPrefix(desc);
  decl << "typedef int32_t " << prefix << ";\n";
  decl << "enum {\n";
  for (int i = 0; i < desc->value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* value = desc->value(i);
    decl << "  " << prefix << "_" << value->name() << " = " << value->number()
         << ",\n";
  }
  decl << "};\n";
  if (rust == nullptr) {
    return;
  }
  const std::string rust_name = RustIdent(FlatEnumName(desc));
  *rust << "#[derive(Clone, Copy, PartialEq, Eq, Debug)]\npub struct " << rust_name
        << "(pub i32);\nimpl " << rust_name << " {\n";
  for (int i = 0; i < desc->value_count(); ++i) {
    const google::protobuf::EnumValueDescriptor* value = desc->value(i);
    *rust << "    pub const " << RustIdent(std::string(value->name()))
          << ": Self = Self(" << value->number() << ");\n";
  }
  *rust << "}\n\n";
}

std::string Guard(const std::string& cpp, bool is_const,
                  const std::string& fallback) {
  std::string out = "  using Msg = " + cpp + ";\n  ";
  out += is_const ? "const auto* h = ::phaser::capi::AsHandle<Msg>(msg);\n"
                  : "auto* h = ::phaser::capi::AsHandle<Msg>(msg);\n";
  out += "  if (h == nullptr || h->get() == nullptr) {\n    " + fallback +
         "\n  }\n";
  return out;
}

void Bind(std::ostream& decl, std::ostream& def, std::ostream* rust_extern,
          std::ostream* rust_body, const std::string& c_signature,
          const std::string& c_body, const std::string& rust_signature,
          const std::string& rust_method) {
  decl << c_signature << ";\n";
  def << c_signature << " {\n" << c_body << "}\n";
  if (rust_extern == nullptr) {
    return;
  }
  *rust_extern << "    fn " << rust_signature << ";\n";
  *rust_body << rust_method;
}

std::string Qualified(const std::string& package, const std::string& added,
                      const std::string& flat) {
  std::string scope = "::";
  if (!package.empty()) {
    scope += absl::StrReplaceAll(package, {{".", "::"}});
  }
  if (!added.empty()) {
    if (scope.size() > 2) {
      scope += "::";
    }
    scope += added;
  }
  if (scope == "::") {
    return "::" + flat;
  }
  return scope + "::" + flat;
}

}  // namespace

std::string MessageGenerator::CppMessageType(
    const google::protobuf::Descriptor* desc) const {
  if (desc->full_name() == "google.protobuf.Any") {
    return "::phaser::AnyMessage";
  }
  return Qualified(std::string(desc->file()->package()), added_namespace_,
                   FlatMessageName(desc));
}

std::string MessageGenerator::CppEnumType(
    const google::protobuf::EnumDescriptor* desc) const {
  return Qualified(std::string(desc->file()->package()), added_namespace_,
                   FlatEnumName(desc));
}

std::string MessageGenerator::RustMessageType(
    const google::protobuf::Descriptor* desc) const {
  if (desc->full_name() == "google.protobuf.Any") {
    return "AnyMessage";
  }
  return RustIdent(FlatMessageName(desc));
}

void MessageGenerator::EmitCField(
    const google::protobuf::FieldDescriptor* field, std::ostream& decl,
    std::ostream& def, std::ostream* rust_extern, std::ostream* rust_body,
    std::vector<std::string>* declared_types, const std::string& self_prefix,
    const std::string& self_cxx, const std::string& self_raw) {
  if (field->type() == google::protobuf::FieldDescriptor::TYPE_GROUP) {
    return;
  }
  const std::string proto_name(field->name());
  const std::string cxx_get = FieldAccessorName(proto_name);
  const std::string rust_get = RustIdent(cxx_get);
  const bool repeated = field->is_repeated();
  const std::string const_self = "const " + self_prefix + "* msg";
  const std::string mut_self = self_prefix + "* msg";
  const std::string raw_const = "*const " + self_raw;
  const std::string raw_mut = "*mut " + self_raw;

  auto c_name = [&](const std::string& method) {
    return self_prefix + "_" + method;
  };

  if (field->type() == google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
    const bool any = IsAny(field);
    const std::string child_cpp = CppMessageType(field->message_type());
    const std::string child_prefix =
        any ? "phaser_AnyMessage" : CMessagePrefix(field->message_type());
    const std::string child_rust = RustMessageType(field->message_type());
    const std::string child_raw =
        any ? "RawAnyMessage" : "Raw" + child_rust;
    if (!any) {
      EnsureType(decl, declared_types, child_prefix);
    }
    const std::string mutable_cxx = "mutable_" + proto_name;
    const std::string add_cxx = "add_" + proto_name;
    const std::string clear_cxx = "clear_" + proto_name;
    const std::string size_cxx = proto_name + "_size";
    const std::string has_cxx = PresenceAccessorName(proto_name);

    auto box_view = [&](const std::string& expr) {
      return "  return ::phaser::capi::BoxView< " + child_cpp + ", " +
             child_prefix + ">(" + expr + ");\n";
    };
    auto box_value = [&](const std::string& expr) {
      return "  return ::phaser::capi::BoxValue< " + child_cpp + ", " +
             child_prefix + ">(" + expr + ");\n";
    };
    auto rust_child = [&](const std::string& call) {
      if (any) {
        return std::string("        AnyMessage::from_raw(unsafe { ") + call +
               " })\n";
      }
      return "        " + child_rust +
             " {\n            ptr: rust_ptr(unsafe { " + call +
             " }),\n            _parent: PhantomData,\n        }\n";
    };

    if (repeated) {
      Bind(decl, def, rust_extern, rust_body,
           child_prefix + "* " + c_name(cxx_get) + "(" + const_self +
               ", size_t index)",
           Guard(self_cxx, true, "return nullptr;") +
               box_value("h->get()->" + cxx_get + "(index)"),
           c_name(cxx_get) + "(msg: " + raw_const + ", index: usize) -> *mut " +
               child_raw,
           "    pub fn " + rust_get +
               "<'b>(&'b self, index: usize) -> " + child_rust + "<'b> {\n" +
               rust_child(c_name(cxx_get) + "(self.ptr, index)") + "    }\n");
      Bind(decl, def, rust_extern, rust_body,
           child_prefix + "* " + c_name(mutable_cxx) + "(" + mut_self +
               ", size_t index)",
           Guard(self_cxx, false, "return nullptr;") +
               box_value("h->get()->" + mutable_cxx + "(index)"),
           c_name(mutable_cxx) + "(msg: " + raw_mut +
               ", index: usize) -> *mut " + child_raw,
           "    pub fn " + RustIdent(mutable_cxx) +
               "<'b>(&'b mut self, index: usize) -> " + child_rust +
               "<'b> {\n" +
               rust_child(c_name(mutable_cxx) + "(self.ptr, index)") +
               "    }\n");
      Bind(decl, def, rust_extern, rust_body,
           child_prefix + "* " + c_name(add_cxx) + "(" + mut_self + ")",
           Guard(self_cxx, false, "return nullptr;") +
               box_value("h->get()->" + add_cxx + "()"),
           c_name(add_cxx) + "(msg: " + raw_mut + ") -> *mut " + child_raw,
           "    pub fn " + RustIdent(add_cxx) + "<'b>(&'b mut self) -> " +
               child_rust + "<'b> {\n" +
               rust_child(c_name(add_cxx) + "(self.ptr)") + "    }\n");
    } else {
      Bind(decl, def, rust_extern, rust_body,
           child_prefix + "* " + c_name(cxx_get) + "(" + const_self + ")",
           Guard(self_cxx, true, "return nullptr;") +
               "  const auto& ref = h->get()->" + cxx_get +
               "();\n  return ::phaser::capi::BoxView< " + child_cpp + ", " +
               child_prefix + ">(const_cast< " + child_cpp + "*>(&ref));\n",
           c_name(cxx_get) + "(msg: " + raw_const + ") -> *mut " + child_raw,
           "    pub fn " + rust_get + "<'b>(&'b self) -> " + child_rust +
               "<'b> {\n" + rust_child(c_name(cxx_get) + "(self.ptr)") +
               "    }\n");
      Bind(decl, def, rust_extern, rust_body,
           child_prefix + "* " + c_name(mutable_cxx) + "(" + mut_self + ")",
           Guard(self_cxx, false, "return nullptr;") +
               box_view("h->get()->" + mutable_cxx + "()"),
           c_name(mutable_cxx) + "(msg: " + raw_mut + ") -> *mut " + child_raw,
           "    pub fn " + RustIdent(mutable_cxx) + "<'b>(&'b mut self) -> " +
               child_rust + "<'b> {\n" +
               rust_child(c_name(mutable_cxx) + "(self.ptr)") + "    }\n");
      Bind(decl, def, rust_extern, rust_body,
           "uint8_t " + c_name(has_cxx) + "(" + const_self + ")",
           Guard(self_cxx, true, "return 0;") +
               "  return h->get()->" + has_cxx +
               "() ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);\n",
           c_name(has_cxx) + "(msg: " + raw_const + ") -> u8",
           "    pub fn " + RustIdent(has_cxx) + "(&self) -> bool {\n        flag(unsafe { " +
               c_name(has_cxx) + "(self.ptr) })\n    }\n");
    }
    Bind(decl, def, rust_extern, rust_body,
         "void " + c_name(clear_cxx) + "(" + mut_self + ")",
         Guard(self_cxx, false, "return;") + "  h->get()->" + clear_cxx +
             "();\n",
         c_name(clear_cxx) + "(msg: " + raw_mut + ")",
         "    pub fn " + RustIdent(clear_cxx) +
             "(&mut self) {\n        unsafe { " + c_name(clear_cxx) +
             "(self.ptr) }\n    }\n");
    if (repeated) {
      Bind(decl, def, rust_extern, rust_body,
           "size_t " + c_name(size_cxx) + "(" + const_self + ")",
           Guard(self_cxx, true, "return 0;") + "  return h->get()->" +
               size_cxx + "();\n",
           c_name(size_cxx) + "(msg: " + raw_const + ") -> usize",
           "    pub fn " + RustIdent(size_cxx) +
               "(&self) -> usize {\n        unsafe { " + c_name(size_cxx) +
               "(self.ptr) }\n    }\n");
      Bind(decl, def, rust_extern, rust_body,
           "void " + c_name("reserve_" + proto_name) + "(" + mut_self +
               ", size_t count)",
           Guard(self_cxx, false, "return;") + "  h->get()->reserve_" +
               proto_name + "(count);\n",
           c_name("reserve_" + proto_name) + "(msg: " + raw_mut +
               ", count: usize)",
           "    pub fn " + RustIdent("reserve_" + proto_name) +
               "(&mut self, count: usize) {\n        unsafe { " +
               c_name("reserve_" + proto_name) + "(self.ptr, count) }\n    }\n");
      Bind(decl, def, rust_extern, rust_body,
           "void " + c_name("resize_" + proto_name) + "(" + mut_self +
               ", size_t count)",
           Guard(self_cxx, false, "return;") + "  h->get()->resize_" +
               proto_name + "(count);\n",
           c_name("resize_" + proto_name) + "(msg: " + raw_mut +
               ", count: usize)",
           "    pub fn " + RustIdent("resize_" + proto_name) +
               "(&mut self, count: usize) {\n        unsafe { " +
               c_name("resize_" + proto_name) + "(self.ptr, count) }\n    }\n");
    }
    return;
  }

  std::string c_type = "int32_t";
  std::string rust_type = "i32";
  std::string c_zero = "0";
  enum class Kind { kNumber, kBool, kString, kBytes, kEnum };
  Kind kind = Kind::kNumber;
  std::string enum_cpp;
  std::string enum_rust;
  switch (field->type()) {
    case google::protobuf::FieldDescriptor::TYPE_INT32:
    case google::protobuf::FieldDescriptor::TYPE_SINT32:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED32:
      break;
    case google::protobuf::FieldDescriptor::TYPE_INT64:
    case google::protobuf::FieldDescriptor::TYPE_SINT64:
    case google::protobuf::FieldDescriptor::TYPE_SFIXED64:
      c_type = "int64_t";
      rust_type = "i64";
      break;
    case google::protobuf::FieldDescriptor::TYPE_UINT32:
    case google::protobuf::FieldDescriptor::TYPE_FIXED32:
      c_type = "uint32_t";
      rust_type = "u32";
      break;
    case google::protobuf::FieldDescriptor::TYPE_UINT64:
    case google::protobuf::FieldDescriptor::TYPE_FIXED64:
      c_type = "uint64_t";
      rust_type = "u64";
      break;
    case google::protobuf::FieldDescriptor::TYPE_FLOAT:
      c_type = "float";
      rust_type = "f32";
      break;
    case google::protobuf::FieldDescriptor::TYPE_DOUBLE:
      c_type = "double";
      rust_type = "f64";
      break;
    case google::protobuf::FieldDescriptor::TYPE_BOOL:
      kind = Kind::kBool;
      c_type = "uint8_t";
      rust_type = "bool";
      break;
    case google::protobuf::FieldDescriptor::TYPE_STRING:
      kind = Kind::kString;
      c_type = "PhaserBytes";
      rust_type = "String";
      c_zero = "PhaserBytes{nullptr, size_t{0}}";
      break;
    case google::protobuf::FieldDescriptor::TYPE_BYTES:
      kind = Kind::kBytes;
      c_type = "PhaserBytes";
      rust_type = "Vec<u8>";
      c_zero = "PhaserBytes{nullptr, size_t{0}}";
      break;
    case google::protobuf::FieldDescriptor::TYPE_ENUM:
      kind = Kind::kEnum;
      c_type = "int32_t";
      enum_cpp = CppEnumType(field->enum_type());
      enum_rust = RustIdent(FlatEnumName(field->enum_type()));
      rust_type = enum_rust;
      break;
    default:
      return;
  }

  const std::string set_cxx = "set_" + proto_name;
  const std::string clear_cxx = "clear_" + proto_name;
  const std::string has_cxx = PresenceAccessorName(proto_name);
  const std::string add_cxx = "add_" + proto_name;
  const std::string size_cxx = proto_name + "_size";

  auto value_expr = [&](const std::string& expr) {
    if (kind == Kind::kBool) {
      return expr + " ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0)";
    }
    if (kind == Kind::kString || kind == Kind::kBytes) {
      return "::phaser::capi::Copy(" + expr + ")";
    }
    if (kind == Kind::kEnum) {
      return "static_cast<int32_t>(" + expr + ")";
    }
    return expr;
  };
  auto store_expr = [&](const std::string& value) {
    if (kind == Kind::kBool) {
      return value + " != 0";
    }
    if (kind == Kind::kString || kind == Kind::kBytes) {
      return "::phaser::capi::Bytes(" + value + ")";
    }
    if (kind == Kind::kEnum) {
      return "static_cast< " + enum_cpp + ">(" + value + ")";
    }
    return value;
  };

  const std::string get_params =
      repeated ? const_self + ", size_t index" : const_self;
  const std::string get_call =
      repeated ? cxx_get + "(index)" : cxx_get + "()";
  const std::string rust_get_params =
      repeated ? "msg: " + raw_const + ", index: usize"
               : "msg: " + raw_const;
  const std::string rust_get_args = repeated ? "self.ptr, index" : "self.ptr";
  std::string rust_ret = rust_type;
  if (kind == Kind::kBool) {
    rust_ret = "u8";
  } else if (kind == Kind::kString || kind == Kind::kBytes) {
    rust_ret = "PhaserBytes";
  } else if (kind == Kind::kEnum) {
    rust_ret = "i32";
  }
  const std::string call =
      "unsafe { " + c_name(cxx_get) + "(" + rust_get_args + ") }";
  std::string expr = call;
  if (kind == Kind::kBool) {
    expr = "flag(" + call + ")";
  } else if (kind == Kind::kString) {
    expr = "copy_string(" + call + ")";
  } else if (kind == Kind::kBytes) {
    expr = "copy_bytes(" + call + ")";
  } else if (kind == Kind::kEnum) {
    expr = enum_rust + "(" + call + ")";
  }
  const std::string rust_get_body = "        " + expr + "\n";
  const std::string rust_get_sig =
      repeated ? "    pub fn " + rust_get + "(&self, index: usize) -> " +
                     rust_type + " {\n" + rust_get_body + "    }\n"
               : "    pub fn " + rust_get + "(&self) -> " + rust_type +
                     " {\n" + rust_get_body + "    }\n";
  Bind(decl, def, rust_extern, rust_body,
       c_type + " " + c_name(cxx_get) + "(" + get_params + ")",
       Guard(self_cxx, true, "return " + c_zero + ";") + "  return " +
           value_expr("h->get()->" + get_call) + ";\n",
       c_name(cxx_get) + "(" + rust_get_params + ") -> " + rust_ret,
       rust_get_sig);

  std::string set_rust_value = rust_type;
  std::string rust_set_arg = "value";
  std::string c_set_type = c_type + " value";
  if (kind == Kind::kBool) {
    c_set_type = "uint8_t value";
    set_rust_value = "bool";
    rust_set_arg = "u8::from(value)";
  } else if (kind == Kind::kString) {
    c_set_type = "const uint8_t* data, size_t len";
    set_rust_value = "&str";
    rust_set_arg = "phaser_bytes_ptr(value.as_bytes()), value.len()";
  } else if (kind == Kind::kBytes) {
    c_set_type = "const uint8_t* data, size_t len";
    set_rust_value = "&[u8]";
    rust_set_arg = "phaser_bytes_ptr(value), value.len()";
  } else if (kind == Kind::kEnum) {
    c_set_type = "int32_t value";
    set_rust_value = enum_rust;
    rust_set_arg = "value.0";
  }
  const std::string stored =
      (kind == Kind::kString || kind == Kind::kBytes) ? store_expr("data, len")
                                                     : store_expr("value");
  if (repeated) {
    Bind(decl, def, rust_extern, rust_body,
         "void " + c_name(set_cxx) + "(" + mut_self + ", size_t index, " +
             c_set_type + ")",
         Guard(self_cxx, false, "return;") + "  h->get()->" + set_cxx +
             "(index, " + stored + ");\n",
         c_name(set_cxx) + "(msg: " + raw_mut + ", index: usize, " +
             (kind == Kind::kString || kind == Kind::kBytes
                  ? "data: *const u8, len: usize"
                  : std::string("value: ") + rust_ret) +
             ")",
         "    pub fn " + RustIdent(set_cxx) +
             "(&mut self, index: usize, value: " + set_rust_value +
             ") {\n        unsafe { " + c_name(set_cxx) +
             "(self.ptr, index, " + rust_set_arg + ") }\n    }\n");
    Bind(decl, def, rust_extern, rust_body,
         "void " + c_name(add_cxx) + "(" + mut_self + ", " + c_set_type + ")",
         Guard(self_cxx, false, "return;") + "  h->get()->" + add_cxx + "(" +
             stored + ");\n",
         c_name(add_cxx) + "(msg: " + raw_mut + ", " +
             (kind == Kind::kString || kind == Kind::kBytes
                  ? "data: *const u8, len: usize"
                  : std::string("value: ") + rust_ret) +
             ")",
         "    pub fn " + RustIdent(add_cxx) + "(&mut self, value: " +
             set_rust_value + ") {\n        unsafe { " + c_name(add_cxx) +
             "(self.ptr, " + rust_set_arg + ") }\n    }\n");
    Bind(decl, def, rust_extern, rust_body,
         "size_t " + c_name(size_cxx) + "(" + const_self + ")",
         Guard(self_cxx, true, "return 0;") + "  return h->get()->" + size_cxx +
             "();\n",
         c_name(size_cxx) + "(msg: " + raw_const + ") -> usize",
         "    pub fn " + RustIdent(size_cxx) +
             "(&self) -> usize {\n        unsafe { " + c_name(size_cxx) +
             "(self.ptr) }\n    }\n");
    Bind(decl, def, rust_extern, rust_body,
         "void " + c_name("reserve_" + proto_name) + "(" + mut_self +
             ", size_t count)",
         Guard(self_cxx, false, "return;") + "  h->get()->reserve_" +
             proto_name + "(count);\n",
         c_name("reserve_" + proto_name) + "(msg: " + raw_mut +
             ", count: usize)",
         "    pub fn " + RustIdent("reserve_" + proto_name) +
             "(&mut self, count: usize) {\n        unsafe { " +
             c_name("reserve_" + proto_name) + "(self.ptr, count) }\n    }\n");
    Bind(decl, def, rust_extern, rust_body,
         "void " + c_name("resize_" + proto_name) + "(" + mut_self +
             ", size_t count)",
         Guard(self_cxx, false, "return;") + "  h->get()->resize_" + proto_name +
             "(count);\n",
         c_name("resize_" + proto_name) + "(msg: " + raw_mut + ", count: usize)",
         "    pub fn " + RustIdent("resize_" + proto_name) +
             "(&mut self, count: usize) {\n        unsafe { " +
             c_name("resize_" + proto_name) + "(self.ptr, count) }\n    }\n");
  } else {
    Bind(decl, def, rust_extern, rust_body,
         "void " + c_name(set_cxx) + "(" + mut_self + ", " + c_set_type + ")",
         Guard(self_cxx, false, "return;") + "  h->get()->" + set_cxx + "(" +
             stored + ");\n",
         c_name(set_cxx) + "(msg: " + raw_mut + ", " +
             (kind == Kind::kString || kind == Kind::kBytes
                  ? "data: *const u8, len: usize"
                  : std::string("value: ") + rust_ret) +
             ")",
         "    pub fn " + RustIdent(set_cxx) + "(&mut self, value: " +
             set_rust_value + ") {\n        unsafe { " + c_name(set_cxx) +
             "(self.ptr, " + rust_set_arg + ") }\n    }\n");
    Bind(decl, def, rust_extern, rust_body,
         "uint8_t " + c_name(has_cxx) + "(" + const_self + ")",
         Guard(self_cxx, true, "return 0;") + "  return h->get()->" + has_cxx +
             "() ? static_cast<uint8_t>(1) : static_cast<uint8_t>(0);\n",
         c_name(has_cxx) + "(msg: " + raw_const + ") -> u8",
         "    pub fn " + RustIdent(has_cxx) +
             "(&self) -> bool {\n        flag(unsafe { " + c_name(has_cxx) +
             "(self.ptr) })\n    }\n");
  }
  Bind(decl, def, rust_extern, rust_body,
       "void " + c_name(clear_cxx) + "(" + mut_self + ")",
       Guard(self_cxx, false, "return;") + "  h->get()->" + clear_cxx + "();\n",
       c_name(clear_cxx) + "(msg: " + raw_mut + ")",
       "    pub fn " + RustIdent(clear_cxx) +
           "(&mut self) {\n        unsafe { " + c_name(clear_cxx) +
           "(self.ptr) }\n    }\n");
}

void MessageGenerator::GenerateBindings(
    std::ostream& decl, std::ostream& def, std::ostream* rust,
    std::vector<std::string>* declared_types) {
  if (!IsCFrontend()) {
    return;
  }
  for (const auto& nested : nested_message_gens_) {
    nested->GenerateBindings(decl, def, rust, declared_types);
  }
  const std::string prefix = CMessagePrefix(message_);
  // Field types named Any are the runtime AnyMessage. This message's own
  // class is the generated type, including when the proto is google.protobuf.Any.
  const std::string cpp =
      Qualified(std::string(message_->file()->package()), added_namespace_,
                FlatMessageName(message_));
  const std::string rust_name = RustIdent(FlatMessageName(message_));
  const std::string raw = "Raw" + rust_name;
  EnsureType(decl, declared_types, prefix);
  for (int i = 0; i < message_->enum_type_count(); ++i) {
    EmitEnum(message_->enum_type(i), decl, rust);
  }

  std::stringstream rust_extern;
  std::stringstream rust_body;
  std::ostream* rust_extern_out = rust == nullptr ? nullptr : &rust_extern;
  std::ostream* rust_body_out = rust == nullptr ? nullptr : &rust_body;
  const std::string const_self = "const " + prefix + "* msg";
  const std::string mut_self = prefix + "* msg";

  Bind(decl, def, rust_extern_out, rust_body_out,
       prefix + "* " + prefix + "_new(size_t initial_size)",
       "  using Msg = " + cpp +
           ";\n  return ::phaser::capi::BoxNew<Msg, " + prefix +
           ">(initial_size);\n",
       prefix + "_new(initial_size: usize) -> *mut " + raw,
       "    pub fn new(initial_size: usize) -> " + rust_name +
           "<'static> {\n        " + rust_name +
           " {\n            ptr: rust_ptr(unsafe { " + prefix +
           "_new(initial_size) }),\n            _parent: PhantomData,\n"
           "        }\n    }\n");
  Bind(decl, def, rust_extern_out, rust_body_out,
       "void " + prefix + "_free(" + mut_self + ")",
       "  using Msg = " + cpp + ";\n  ::phaser::capi::FreeHandle<Msg>(msg);\n",
       prefix + "_free(msg: *mut " + raw + ")", "");
  Bind(decl, def, rust_extern_out, rust_body_out,
       "void " + prefix + "_clear(" + mut_self + ")",
       Guard(cpp, false, "return;") + "  h->get()->Clear();\n",
       prefix + "_clear(msg: *mut " + raw + ")",
       "    pub fn clear(&mut self) {\n        unsafe { " + prefix +
           "_clear(self.ptr) }\n    }\n");
  Bind(decl, def, rust_extern_out, rust_body_out,
       "PhaserBytes " + prefix + "_debug_string(" + const_self + ")",
       Guard(cpp, true, "return PhaserBytes{nullptr, size_t{0}};") +
           "  return ::phaser::capi::Copy(h->get()->DebugString());\n",
       prefix + "_debug_string(msg: *const " + raw + ") -> PhaserBytes",
       "    pub fn debug_string(&self) -> String {\n        copy_string(unsafe { " +
           prefix + "_debug_string(self.ptr) })\n    }\n");
  Bind(decl, def, rust_extern_out, rust_body_out,
       "size_t " + prefix + "_byte_size_long(" + const_self + ")",
       Guard(cpp, true, "return 0;") + "  return h->get()->ByteSizeLong();\n",
       prefix + "_byte_size_long(msg: *const " + raw + ") -> usize",
       "    pub fn byte_size_long(&self) -> usize {\n        unsafe { " +
           prefix + "_byte_size_long(self.ptr) }\n    }\n");
  Bind(decl, def, rust_extern_out, rust_body_out,
       "PhaserBytes " + prefix + "_serialize_as_string(" + const_self + ")",
       Guard(cpp, true, "return PhaserBytes{nullptr, size_t{0}};") +
           "  return ::phaser::capi::Copy(h->get()->SerializeAsString());\n",
       prefix + "_serialize_as_string(msg: *const " + raw + ") -> PhaserBytes",
       "    pub fn serialize_as_string(&self) -> Vec<u8> {\n        copy_bytes(unsafe { " +
           prefix + "_serialize_as_string(self.ptr) })\n    }\n");
  Bind(decl, def, rust_extern_out, rust_body_out,
       "uint8_t " + prefix + "_parse_from_string(" + mut_self +
           ", const uint8_t* data, size_t len)",
       Guard(cpp, false, "return 0;") +
           "  std::string bytes;\n  if (data != nullptr && len != 0) {\n"
           "    bytes.assign(reinterpret_cast<const char*>(data), len);\n  }\n"
           "  return h->get()->ParseFromString(bytes) ? static_cast<uint8_t>(1)"
           " : static_cast<uint8_t>(0);\n",
       prefix + "_parse_from_string(msg: *mut " + raw +
           ", data: *const u8, len: usize) -> u8",
       "    pub fn parse_from_string(&mut self, bytes: &[u8]) -> bool {\n"
       "        flag(unsafe { " +
           prefix +
           "_parse_from_string(self.ptr, phaser_bytes_ptr(bytes), bytes.len()) })\n"
           "    }\n");

  for (const auto& field : fields_) {
    EmitCField(field->field, decl, def, rust_extern_out, rust_body_out,
               declared_types, prefix, cpp, raw);
  }
  for (const auto& [oneof, info] : unions_) {
    const std::string case_cxx = std::string(oneof->name()) + "_case";
    Bind(decl, def, rust_extern_out, rust_body_out,
         "int32_t " + prefix + "_" + case_cxx + "(" + const_self + ")",
         Guard(cpp, true, "return 0;") +
             "  return static_cast<int32_t>(h->get()->" + case_cxx + "());\n",
         prefix + "_" + case_cxx + "(msg: *const " + raw + ") -> i32",
         "    pub fn " + RustIdent(case_cxx) +
             "(&self) -> i32 {\n        unsafe { " + prefix + "_" + case_cxx +
             "(self.ptr) }\n    }\n");
    for (const auto& member : info->members) {
      EmitCField(member->field, decl, def, rust_extern_out, rust_body_out,
                 declared_types, prefix, cpp, raw);
    }
  }

  if (rust == nullptr) {
    return;
  }
  *rust << "#[repr(C)]\nstruct " << raw << " {\n    _private: [u8; 0],\n}\n\n";
  *rust << "extern \"C\" {\n" << rust_extern.str() << "}\n\n";
  *rust << "pub struct " << rust_name << "<'a> {\n    ptr: *mut " << raw
        << ",\n    _parent: PhantomData<&'a ()>,\n}\n\n";
  *rust << "impl<'a> Drop for " << rust_name << "<'a> {\n";
  *rust << "    fn drop(&mut self) {\n        if !self.ptr.is_null() {\n";
  *rust << "            unsafe { " << prefix << "_free(self.ptr) }\n";
  *rust << "            self.ptr = std::ptr::null_mut();\n        }\n    }\n}\n\n";
  *rust << "impl<'a> " << rust_name << "<'a> {\n" << rust_body.str() << "}\n\n";
}

void Generator::GenerateBindings(std::ostream& c_header, std::ostream& c_source,
                                 std::ostream* rust) {
  if (frontend_style_ != FrontendStyle::kC) {
    return;
  }
  c_header << "#pragma once\n#include \"phaser/runtime/phaser_c.h\"\n\n";
  c_header << "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n";
  std::filesystem::path include_path(GeneratedIncludeFilename(
      package_name_, target_name_, std::string(file_->name())));
  include_path.replace_extension(".phaser.c.h");
  c_source << "\n#include \"phaser/runtime/phaser_c_handle.h\"\n#include \""
           << include_path.string() << "\"\nextern \"C\" {\n";
  for (int i = 0; i < file_->enum_type_count(); ++i) {
    EmitEnum(file_->enum_type(i), c_header, rust);
  }
  std::vector<std::string> declared;
  for (auto& message : message_gens_) {
    message->GenerateBindings(c_header, c_source, rust, &declared);
  }
  c_header << "\n#ifdef __cplusplus\n}\n#endif\n";
  c_source << "}  // extern \"C\"\n";
}

}  // namespace phaser
