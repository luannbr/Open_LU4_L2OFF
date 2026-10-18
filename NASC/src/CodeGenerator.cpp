#include "CodeGenerator.h"

#include <array>
#include <charconv>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace nasc {
namespace {

void AppendAscii(std::u16string& output, std::string_view text) {
    for (const char character : text) {
        output.push_back(static_cast<char16_t>(static_cast<unsigned char>(character)));
    }
}

template <typename Integer>
void AppendDecimal(std::u16string& output, Integer value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("decimal formatting failed");
    }
    AppendAscii(output, {buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())});
}

void AppendFixedSix(std::u16string& output, float value) {
    // The original prints floats with %f, so with six digits after the point.
    // std::to_chars gives the same text in every locale.
    std::array<char, 128> buffer{};
    const auto result =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), static_cast<double>(value), std::chars_format::fixed, 6);
    if (result.ec != std::errc{}) {
        throw std::runtime_error("floating-point formatting failed");
    }
    AppendAscii(output, {buffer.data(), static_cast<std::size_t>(result.ptr - buffer.data())});
}

void AppendText(std::u16string& output, OptionalText text) {
    if (text) {
        output.append(*text);
    } else {
        output.append(u"(null)");
    }
}

std::u16string Instruction(std::u16string_view opcode) {
    std::u16string result(u"\t");
    result.append(opcode);
    result.append(u"\r\n");
    return result;
}

} // namespace

CodeGenerator::CodeGenerator(std::int32_t initialCodeLabel, std::int32_t initialStringLabel) noexcept :
    stringLabel_(initialStringLabel), codeLabel_(initialCodeLabel) {}

std::u16string_view CodeGenerator::Output() const noexcept { return output_; }

const std::vector<std::u16string>& CodeGenerator::PendingCode() const noexcept { return code_; }

std::int32_t CodeGenerator::DummyStackPoint() const noexcept { return dummyStackPoint_; }

std::int32_t CodeGenerator::NextCodeLabel() const noexcept { return codeLabel_; }

std::int32_t CodeGenerator::NextStringLabel() const noexcept { return stringLabel_; }

void CodeGenerator::AppendDirect(std::u16string text) { output_.append(text); }

void CodeGenerator::AppendCode(std::u16string text) { code_.push_back(std::move(text)); }

void CodeGenerator::WriteVersions(
    std::int32_t pointerSize,
    std::int32_t sharedFactoryVersion,
    std::int32_t npchVersion,
    std::int32_t nascVersion,
    std::int32_t npcEventHVersion,
    std::int32_t debug
) {
    std::u16string line(u"SizeofPointer ");
    AppendDecimal(line, pointerSize);
    line.append(u"\r\nSharedFactoryVersion ");
    AppendDecimal(line, sharedFactoryVersion);
    line.append(u"\r\nNPCHVersion ");
    AppendDecimal(line, npchVersion);
    line.append(u"\r\nNASCVersion ");
    AppendDecimal(line, nascVersion);
    line.append(u"\r\nNPCEventHVersion ");
    AppendDecimal(line, npcEventHVersion);
    line.append(u"\r\nDebug ");
    AppendDecimal(line, debug);
    line.append(u"\r\n\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::NewClass(ClassKind kind, std::int32_t id, std::u16string_view name, OptionalText baseName) {
    std::u16string line;
    switch (kind) {
        case ClassKind::Normal:
            line.append(u"class ");
            break;
        case ClassKind::Development:
            line.append(u"dev_class ");
            break;
        case ClassKind::Test:
            line.append(u"ts_class ");
            break;
        case ClassKind::Broadcast:
            line.append(u"bc_class ");
            break;
    }
    AppendDecimal(line, id);
    line.push_back(u' ');
    line.append(name);
    line.append(u" : ");
    AppendText(line, baseName);
    line.append(u"\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::ClassEnd() { AppendDirect(u"class_end\r\n\r\n\r\n"); }

void CodeGenerator::ParameterDefineBegin() { AppendDirect(u"parameter_define_begin\r\n"); }

void CodeGenerator::ParameterDefineEnd() { AppendDirect(u"parameter_define_end\r\n"); }

void CodeGenerator::PropertyDefineBegin() { AppendDirect(u"property_define_begin\r\n"); }

void CodeGenerator::PropertyDefineEnd() { AppendDirect(u"property_define_end\r\n"); }

void CodeGenerator::IntType(std::u16string_view name, std::int64_t value) {
    std::u16string line(u"\tint ");
    line.append(name);
    line.push_back(u' ');
    AppendDecimal(line, value);
    line.append(u"\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::FloatType(std::u16string_view name, float value) {
    std::u16string line(u"\tfloat ");
    line.append(name);
    line.push_back(u' ');
    AppendFixedSix(line, value);
    line.append(u"\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::StringType(std::u16string_view name, std::u16string_view value) {
    std::u16string line(u"\tstring ");
    line.append(name);
    line.append(u" \"");
    line.append(value);
    line.append(u"\"\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::WaypointsType(std::u16string_view name) {
    std::u16string line(u"\twaypointstype ");
    line.append(name);
    line.append(u"\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::WaypointDelaysType(std::u16string_view name) {
    std::u16string line(u"\twaypointdelaystype ");
    line.append(name);
    line.append(u"\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::TelposListBegin(std::u16string_view name) {
    std::u16string line(u"\ttelposlist_begin ");
    line.append(name);
    line.append(u"\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::TelposListEnd() { AppendDirect(u"\ttelposlist_end\r\n"); }

void CodeGenerator::Telpos(
    std::u16string_view name, std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t heading, std::int32_t world
) {
    std::u16string line(u"\t\t{\"");
    line.append(name);
    line.append(u"\"; ");
    AppendDecimal(line, x);
    line.append(u"; ");
    AppendDecimal(line, y);
    line.append(u"; ");
    AppendDecimal(line, z);
    line.append(u"; ");
    AppendDecimal(line, heading);
    line.append(u"; ");
    AppendDecimal(line, world);
    line.append(u" }\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::Telpos2(
    std::int32_t first, std::int32_t second, std::int32_t third, std::int32_t fourth, std::int32_t fifth, std::int32_t sixth
) {
    std::u16string line(u"\t\t{");
    AppendDecimal(line, first);
    line.append(u"; ");
    AppendDecimal(line, second);
    line.append(u"; ");
    AppendDecimal(line, third);
    line.append(u"; ");
    AppendDecimal(line, fourth);
    line.append(u"; ");
    AppendDecimal(line, fifth);
    line.append(u"; ");
    AppendDecimal(line, sixth);
    line.append(u" }\r\n");
    AppendDirect(std::move(line));
}

std::u16string CodeGenerator::IntList(std::span<const std::int32_t> values) {
    // Zero items are skipped, but the last delimiter depends on the item index,
    // not on the number of written items. This matches the original output.
    std::u16string list(u"{");
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index] == 0) {
            continue;
        }
        AppendDecimal(list, values[index]);
        list.append(index + 1 == values.size() ? u"}" : u"; ");
    }
    return list;
}

void CodeGenerator::TelposEx(
    std::u16string_view name,
    std::int32_t x,
    std::int32_t y,
    std::int32_t z,
    std::int32_t heading,
    std::span<const std::int32_t> extra
) {
    std::u16string line(u"\t\t{\"");
    line.append(name);
    line.append(u"\"; ");
    AppendDecimal(line, x);
    line.append(u"; ");
    AppendDecimal(line, y);
    line.append(u"; ");
    AppendDecimal(line, z);
    line.append(u"; ");
    AppendDecimal(line, heading);
    line.append(u"; ");
    line.append(IntList(extra));
    line.append(u"}\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::TelposEx2(
    std::int32_t first,
    std::int32_t second,
    std::int32_t third,
    std::int32_t fourth,
    std::int32_t fifth,
    std::span<const std::int32_t> extra
) {
    std::u16string line(u"\t\t{");
    AppendDecimal(line, first);
    line.append(u"; ");
    AppendDecimal(line, second);
    line.append(u"; ");
    AppendDecimal(line, third);
    line.append(u"; ");
    AppendDecimal(line, fourth);
    line.append(u"; ");
    AppendDecimal(line, fifth);
    line.append(u"; ");
    line.append(IntList(extra));
    line.append(u"}\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::BuysellListBegin(std::u16string_view name) {
    std::u16string line(u"\tbuyselllist_begin ");
    line.append(name);
    line.append(u"\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::BuysellListEnd() { AppendDirect(u"\tbuyselllist_end\r\n"); }

void CodeGenerator::Buysell(std::int32_t itemId, std::int64_t count, float chance, std::int64_t price) {
    std::u16string line(u"\t\t{");
    AppendDecimal(line, itemId);
    line.append(u"; ");
    AppendDecimal(line, count);
    line.append(u"; ");
    AppendFixedSix(line, chance);
    line.append(u"; ");
    AppendDecimal(line, price);
    line.append(u" }\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::Buy(std::int32_t itemId, std::int32_t count) {
    std::u16string line(u"\t\t{");
    AppendDecimal(line, itemId);
    line.append(u"; ");
    AppendDecimal(line, count);
    line.append(u" }\r\n");
    AppendDirect(std::move(line));
}

void CodeGenerator::InitHandler() {
    // The original does not clear the handler name here.
    handlerId_ = 0;
    localVariables_.clear();
    code_.clear();
}

void CodeGenerator::SetHandler(std::int32_t id, std::u16string_view name) {
    handlerId_ = id;
    handlerName_.assign(name);
}

void CodeGenerator::SetLocalVariables(std::span<const std::u16string> variables) {
    localVariables_.assign(variables.begin(), variables.end());
}

void CodeGenerator::VariableList(std::span<const std::u16string> variables) {
    AppendDirect(u"\tvariable_begin\r\n");
    for (const std::u16string& variable : variables) {
        std::u16string line(u"\t\t\"");
        line.append(variable);
        line.append(u"\"\r\n");
        AppendDirect(std::move(line));
    }
    AppendDirect(u"\tvariable_end\r\n\r\n");
}

void CodeGenerator::FlushHandler() {
    std::u16string header(u"handler ");
    AppendDecimal(header, handlerId_);
    header.push_back(u' ');
    AppendDecimal(header, static_cast<std::int32_t>(code_.size()));
    header.append(u"\t//  ");
    header.append(handlerName_);
    header.append(u"\r\n");
    AppendDirect(std::move(header));
    VariableList(localVariables_);
    for (const std::u16string& line : code_) {
        output_.append(line);
    }
    AppendDirect(u"handler_end\r\n\r\n");
}

void CodeGenerator::AppendBinaryInstruction(std::u16string_view opcode) {
    AppendCode(Instruction(opcode));
    --dummyStackPoint_;
}

void CodeGenerator::Add() { AppendBinaryInstruction(u"add"); }

std::int32_t CodeGenerator::StringOperandKind(const ScriptTypeDescriptor* type) noexcept {
    // Compare the entries of the type table, not only their IDs, like the
    // original.
    if (type == FindScriptType(2) || type == FindScriptType(6)) {
        return 2;
    }
    if (type == FindScriptType(3) || type == FindScriptType(5)) {
        return 3;
    }
    if (type == FindScriptType(4)) {
        return 4;
    }
    return 0;
}

void CodeGenerator::AddString(const ScriptTypeDescriptor* left, const ScriptTypeDescriptor* right) {
    // Operand kinds: (left kind << 8) | right kind.
    const std::int32_t kind = (StringOperandKind(left) << 8) | StringOperandKind(right);
    std::u16string line(u"\tadd_string ");
    AppendDecimal(line, kind);
    line.append(u"\r\n");
    AppendCode(std::move(line));
    --dummyStackPoint_;
}

void CodeGenerator::Subtract() { AppendBinaryInstruction(u"sub"); }
void CodeGenerator::Multiply() { AppendBinaryInstruction(u"mul"); }
void CodeGenerator::Divide() { AppendBinaryInstruction(u"div"); }
void CodeGenerator::Modulo() { AppendBinaryInstruction(u"mod"); }

void CodeGenerator::LogicalNot() { AppendCode(Instruction(u"not")); }

void CodeGenerator::Negate() { AppendCode(Instruction(u"negate")); }

void CodeGenerator::LogicalAnd() { AppendBinaryInstruction(u"and"); }
void CodeGenerator::LogicalOr() { AppendBinaryInstruction(u"or"); }
void CodeGenerator::BitwiseAnd() { AppendBinaryInstruction(u"bit_and"); }
void CodeGenerator::BitwiseOr() { AppendBinaryInstruction(u"bit_or"); }
void CodeGenerator::BitwiseXor() { AppendBinaryInstruction(u"xor"); }
void CodeGenerator::Greater() { AppendBinaryInstruction(u"greater"); }
void CodeGenerator::GreaterEqual() { AppendBinaryInstruction(u"greater_equal"); }
void CodeGenerator::Less() { AppendBinaryInstruction(u"less"); }
void CodeGenerator::LessEqual() { AppendBinaryInstruction(u"less_equal"); }
void CodeGenerator::Equal() { AppendBinaryInstruction(u"equal"); }
void CodeGenerator::NotEqual() { AppendBinaryInstruction(u"not_equal"); }

void CodeGenerator::Assign() {
    AppendCode(Instruction(u"assign"));
    dummyStackPoint_ -= 2;
}

void CodeGenerator::Assign4() {
    AppendCode(Instruction(u"assign4"));
    dummyStackPoint_ -= 2;
}

void CodeGenerator::Fetch(const ScriptTypeDescriptor* type, OptionalText commentText) {
    std::u16string line;
    if (type == FindScriptType(3)) {
        line.append(u"\tfetch_f");
    } else if (type == FindScriptType(5)) {
        line.append(u"\tfetch_d");
    } else if (type == FindScriptType(2)) {
        line.append(u"\tfetch_i4");
    } else {
        line.append(u"\tfetch_i");
    }
    if (commentText) {
        line.append(u"\t\t\t//");
        line.append(*commentText);
    }
    line.append(u"\r\n");
    AppendCode(std::move(line));
}

void CodeGenerator::FlushFetch(const ScriptTypeDescriptor* type, std::int32_t& fetchCount) {
    for (std::int32_t index = 0; index < fetchCount; ++index) {
        Fetch(type);
    }
    fetchCount = 0;
}

void CodeGenerator::PushRegisterSp() {
    AppendCode(Instruction(u"push_reg_sp"));
    ++dummyStackPoint_;
}

void CodeGenerator::PushConst(std::int64_t value, OptionalText commentText) {
    std::u16string line(u"\tpush_const ");
    AppendDecimal(line, value);
    if (commentText) {
        line.append(u"\t\t\t//");
        line.append(*commentText);
    }
    line.append(u"\r\n");
    AppendCode(std::move(line));
    ++dummyStackPoint_;
}

void CodeGenerator::PushConstFloat(float value, OptionalText commentText) {
    std::u16string line(u"\tpush_const ");
    AppendFixedSix(line, value);
    if (commentText) {
        line.append(u"\t\t\t//");
        line.append(*commentText);
    }
    line.append(u"\r\n");
    AppendCode(std::move(line));
    ++dummyStackPoint_;
}

void CodeGenerator::PushStringLabel(std::int32_t label) {
    std::u16string line(u"\tpush_string S");
    AppendDecimal(line, label);
    line.append(u"\r\n");
    AppendCode(std::move(line));
    ++dummyStackPoint_;
}

void CodeGenerator::WriteLabel(std::int32_t label) {
    std::u16string line(u"L");
    AppendDecimal(line, label);
    line.append(u"\r\n");
    AppendCode(std::move(line));
}

void CodeGenerator::JumpFunction(std::u16string_view name) {
    std::u16string line(u"\tjump ");
    line.append(name);
    line.append(u"\r\n");
    AppendCode(std::move(line));
}

void CodeGenerator::JumpLabel(std::int32_t label) {
    std::u16string line(u"\tjump L");
    AppendDecimal(line, label);
    line.append(u"\r\n");
    AppendCode(std::move(line));
}

void CodeGenerator::BranchFalse(std::int32_t label) {
    std::u16string line(u"\tbranch_false L");
    AppendDecimal(line, label);
    line.append(u"\r\n");
    AppendCode(std::move(line));
    --dummyStackPoint_;
}

void CodeGenerator::BranchTrue(std::int32_t label) {
    std::u16string line(u"\tbranch_true L");
    AppendDecimal(line, label);
    line.append(u"\r\n");
    AppendCode(std::move(line));
    --dummyStackPoint_;
}

void CodeGenerator::ShiftSp(std::int32_t amount) {
    std::u16string line(u"\tshift_sp ");
    AppendDecimal(line, amount);
    line.append(u"\r\n");
    AppendCode(std::move(line));
    dummyStackPoint_ += amount;
}

std::int32_t CodeGenerator::DeclareString(std::u16string_view value) {
    const std::int32_t label = stringLabel_++;
    std::u16string line(u"S");
    AppendDecimal(line, label);
    line.append(u".\t\"");
    line.append(value);
    line.append(u"\"\r\n");
    AppendCode(std::move(line));
    return label;
}

void CodeGenerator::ExitHandler() { AppendCode(Instruction(u"exit_handler")); }

void CodeGenerator::CallSuper() { AppendCode(Instruction(u"call_super")); }

void CodeGenerator::PushEvent(std::u16string_view name) {
    std::u16string line(u"\tpush_event\t//  ");
    line.append(name);
    line.append(u"\r\n");
    AppendCode(std::move(line));
    ++dummyStackPoint_;
}

void CodeGenerator::FunctionCall(std::uint64_t encodedIndex, std::u16string_view name) {
    // The original prints this unsigned 64-bit value with %d, so only the low
    // 32 bits appear, as a signed number.
    const auto low = static_cast<std::uint32_t>(encodedIndex);
    const auto printed = static_cast<std::int32_t>(low);
    std::u16string line(u"\tfunc_call ");
    AppendDecimal(line, printed);
    line.append(u"\t//  func[");
    line.append(name);
    line.append(u"]\r\n");
    AppendCode(std::move(line));
}

void CodeGenerator::PushProperty(std::u16string_view name) {
    std::u16string line(u"\tpush_property ");
    line.append(name);
    line.append(u"\r\n");
    AppendCode(std::move(line));
    ++dummyStackPoint_;
}

void CodeGenerator::PushParameter(std::u16string_view name) {
    std::u16string line(u"\tpush_parameter ");
    line.append(name);
    line.append(u"\r\n");
    AppendCode(std::move(line));
    ++dummyStackPoint_;
}

void CodeGenerator::Comment(std::u16string_view) noexcept {}

std::int32_t CodeGenerator::NewCodeLabel() noexcept { return codeLabel_++; }

std::int32_t CodeGenerator::NewStringLabel() noexcept { return stringLabel_++; }

std::int32_t CodeGenerator::PopStack(std::vector<std::int32_t>& stack, std::string_view name) {
    if (stack.empty()) {
        throw std::logic_error(std::string("empty ") + std::string(name));
    }
    const std::int32_t label = stack.back();
    stack.pop_back();
    return label;
}

void CodeGenerator::PushLabel(std::int32_t label) { labels_.push_back(label); }
std::int32_t CodeGenerator::PopLabel() { return PopStack(labels_, "label stack"); }
void CodeGenerator::PushIfLabel(std::int32_t label) { ifLabels_.push_back(label); }
std::int32_t CodeGenerator::PopIfLabel() { return PopStack(ifLabels_, "if-label stack"); }
void CodeGenerator::PushSelectLabel(std::int32_t label) { selectLabels_.push_back(label); }
std::int32_t CodeGenerator::PopSelectLabel() { return PopStack(selectLabels_, "select-label stack"); }
void CodeGenerator::PushAndLabel(std::int32_t label) { andLabels_.push_back(label); }
std::int32_t CodeGenerator::PopAndLabel() { return PopStack(andLabels_, "and-label stack"); }
void CodeGenerator::PushOrLabel(std::int32_t label) { orLabels_.push_back(label); }
std::int32_t CodeGenerator::PopOrLabel() { return PopStack(orLabels_, "or-label stack"); }

} // namespace nasc
