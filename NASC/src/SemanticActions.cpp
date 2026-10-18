#include "SemanticActions.h"

#include "AISymbolDatabase.h"
#include "PrecompiledHeaderDB.h"
#include "ScriptMetadata.h"

#include <array>
#include <bit>
#include <charconv>
#include <stdexcept>
#include <utility>

namespace nasc {
namespace {

[[nodiscard]] bool IsStringAddOperand(const ScriptTypeDescriptor* type) noexcept {
    return type == FindScriptType(2) || type == FindScriptType(3) || type == FindScriptType(4) ||
        type == FindScriptType(5) || type == FindScriptType(6);
}

void AppendDecimal(std::u16string& output, std::int64_t value) {
    std::array<char, 32> buffer{};
    const auto converted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (converted.ec != std::errc{}) {
        throw std::runtime_error("semantic diagnostic formatting failed");
    }
    for (const char character : std::string_view(buffer.data(), static_cast<std::size_t>(converted.ptr - buffer.data()))) {
        output.push_back(static_cast<char16_t>(static_cast<unsigned char>(character)));
    }
}

[[nodiscard]] std::u16string UndefinedFunction(std::u16string_view name) {
    std::u16string message(u"Undefined function[");
    message.append(name);
    message.push_back(u']');
    return message;
}

[[nodiscard]] std::u16string BracketedNameError(std::u16string_view prefix, std::u16string_view name) {
    std::u16string message(prefix);
    message.append(name);
    message.push_back(u']');
    return message;
}

[[nodiscard]] std::u16string MissingField(std::u16string_view name) {
    std::u16string message(u"field[");
    message.append(name);
    message.append(u"] doesn't exist");
    return message;
}

[[nodiscard]] std::u16string MissingAliasedField(std::u16string_view object, std::u16string_view variable) {
    std::u16string message(u"field[");
    message.append(object);
    message.append(u"][");
    message.append(variable);
    message.append(u"] doesn't exist");
    return message;
}

[[nodiscard]] std::u16string ForbiddenAliasVariable(std::u16string_view name) {
    std::u16string message(u"Variable name [");
    message.append(name);
    message.append(u"] is not allowed to use because it's defined by alias");
    return message;
}

[[nodiscard]] std::u16string UndefinedEvent(std::u16string_view name) {
    return BracketedNameError(u"Undefined Event[", name);
}

[[nodiscard]] std::u16string DuplicatedEvent(std::u16string_view event, std::u16string_view className) {
    std::u16string message(u"Duplicated Event Handler [");
    message.append(event);
    message.append(u"] in Class [");
    message.append(className);
    message.push_back(u']');
    return message;
}

[[nodiscard]] std::u16string IncompatibleParameter(std::u16string_view name, std::size_t index, bool includeName) {
    std::u16string message;
    if (includeName) {
        message.push_back(u'[');
        message.append(name);
        message.append(u"] ");
    }
    message.append(u"Parameter[");
    AppendDecimal(message, static_cast<std::int64_t>(index));
    message.append(u"] is not compatible");
    return message;
}

[[nodiscard]] std::u16string ParameterCountError(std::u16string_view name, std::size_t expected, std::size_t actual) {
    std::u16string message(u"[");
    message.append(name);
    message.append(u"] parameter count is different. valid argument count is ");
    AppendDecimal(message, static_cast<std::int64_t>(expected));
    message.append(u". not ");
    AppendDecimal(message, static_cast<std::int64_t>(actual));
    return message;
}

[[nodiscard]] std::uint32_t EncodedFunctionIndex(const ScriptFunctionDescriptor& function) noexcept {
    return (static_cast<std::uint32_t>(function.ownerTypeId) << 24U) |
        (static_cast<std::uint32_t>(function.argumentCount) << 16U) | (static_cast<std::uint32_t>(function.index) & 0xffffU);
}

[[nodiscard]] std::int32_t LowI32(std::int64_t value) noexcept {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits));
}

template <typename TypeMap>
void AppendSymbolData(std::u16string& output, std::u16string_view prefix, const TypeMap& values) {
    if (values.empty()) {
        return;
    }
    output.append(prefix);
    bool first = true;
    for (const auto& [name, type] : values) {
        if (!first) {
            output.push_back(u',');
        }
        first = false;
        output.append(name);
        output.push_back(u':');
        if (type == nullptr) {
            throw std::logic_error("symbol contains an invalid type ID");
        }
        AppendDecimal(output, type->id);
    }
    output.append(u"\r\n");
}

} // namespace

AISemanticValue AISemanticValue::FromInteger(std::int64_t value) {
    AISemanticValue result;
    result.kind = AISemanticKind::Integer;
    result.integer = value;
    return result;
}

AISemanticValue AISemanticValue::FromFloat(float value) {
    AISemanticValue result;
    result.kind = AISemanticKind::Floating;
    result.floating = value;
    return result;
}

AISemanticValue AISemanticValue::FromText(std::u16string_view value) {
    AISemanticValue result;
    result.kind = AISemanticKind::Text;
    result.text.assign(value);
    return result;
}

AISemanticValue AISemanticValue::FromType(const ScriptTypeDescriptor* value) {
    AISemanticValue result;
    result.kind = AISemanticKind::ScriptType;
    result.scriptType = value;
    return result;
}

AISemanticValue AISemanticValue::FromTypeList(std::vector<const ScriptTypeDescriptor*> value) {
    AISemanticValue result;
    result.kind = AISemanticKind::TypeList;
    result.typeList = std::move(value);
    return result;
}

AISemanticValue AISemanticValue::FromIntegerList(std::vector<std::int32_t> value) {
    AISemanticValue result;
    result.kind = AISemanticKind::IntegerList;
    result.integerList = std::move(value);
    return result;
}

AISemanticActions::AISemanticActions(CodeGenerator& codeGenerator, std::int32_t eventTypeId) :
    codeGenerator_(codeGenerator) {
    SetEventTypeId(eventTypeId);
}

bool AISemanticActions::Implements(std::int16_t action) const noexcept { return action >= 0 && action <= 129; }

const AISemanticValue& AISemanticActions::FromTop(std::span<const AISemanticValue> stack, std::size_t offset) {
    if (offset >= stack.size()) {
        throw std::logic_error("semantic stack underflow");
    }
    return stack[stack.size() - 1U - offset];
}

const ScriptTypeDescriptor* AISemanticActions::ValueType(const AISemanticValue& value) {
    if (value.kind != AISemanticKind::ScriptType || value.scriptType == nullptr) {
        throw std::logic_error("yyattribute is not a ScriptTypeInfo pointer");
    }
    return value.scriptType;
}

std::u16string_view AISemanticActions::ValueText(const AISemanticValue& value) {
    if (value.kind != AISemanticKind::Text) {
        throw std::logic_error("yyattribute is not a wide string pointer");
    }
    return value.text;
}

std::int64_t AISemanticActions::ValueInteger(const AISemanticValue& value) {
    if (value.kind != AISemanticKind::Integer) {
        throw std::logic_error("yyattribute is not an integer constant");
    }
    return value.integer;
}

float AISemanticActions::ValueFloat(const AISemanticValue& value) {
    if (value.kind != AISemanticKind::Floating) {
        throw std::logic_error("yyattribute is not a floating constant");
    }
    return value.floating;
}

AISemanticValue AISemanticActions::DefaultValue(std::span<const AISemanticValue> stack, std::int16_t reductionLength) const {
    if (reductionLength < 0) {
        throw std::logic_error("reduction has a negative length");
    }
    if (reductionLength == 0) {
        return {};
    }
    const std::size_t length = static_cast<std::size_t>(reductionLength);
    if (length > stack.size()) {
        throw std::logic_error("semantic reduction underflow");
    }
    // Like the original parser, $$ starts as $1.
    return stack[stack.size() - length];
}

void AISemanticActions::Error(std::u16string_view message) {
    diagnostics_.emplace_back(message);
    diagnosticLines_.push_back(sourceLine_);
}

void AISemanticActions::SetSourceLine(std::size_t line) noexcept { sourceLine_ = line; }

std::span<const std::size_t> AISemanticActions::DiagnosticLines() const noexcept { return diagnosticLines_; }

void AISemanticActions::Flush(const ScriptTypeDescriptor* type) { codeGenerator_.FlushFetch(type, fetchCount_); }

std::int32_t AISemanticActions::FetchCount() const noexcept { return fetchCount_; }

void AISemanticActions::SetFetchCount(std::int32_t count) {
    if (count < 0) {
        throw std::invalid_argument("fetch count cannot be negative");
    }
    fetchCount_ = count;
}

std::int32_t AISemanticActions::EventTypeId() const noexcept { return eventTypeId_; }

void AISemanticActions::SetEventTypeId(std::int32_t eventTypeId) {
    if (FindScriptEventType(eventTypeId) == nullptr) {
        throw std::invalid_argument("event type ID is outside 0..1");
    }
    eventTypeId_ = eventTypeId;
}

void AISemanticActions::SetClassName(std::u16string name) { className_ = std::move(name); }

void AISemanticActions::SetClassKind(ClassKind kind) noexcept { classKind_ = kind; }

void AISemanticActions::RegisterKnownClass(std::int32_t eventTypeId, std::u16string name) {
    if (FindScriptEventType(eventTypeId) == nullptr) {
        throw std::invalid_argument("known class event type is invalid");
    }
    knownClasses_.emplace(eventTypeId, std::move(name));
}

void AISemanticActions::DefinePrecompiledSymbol(std::u16string name, std::int64_t value) {
    precompiledSymbols_.insert_or_assign(std::move(name), value);
}

void AISemanticActions::ImportPrecompiledHeader(const PrecompiledHeaderDB& database) {
    for (const auto& [name, value] : database.Names()) {
        precompiledSymbols_.insert_or_assign(name, value);
    }
}

void AISemanticActions::ImportSymbolDatabase(const AISymbolDatabase& database) {
    knownClasses_.insert(database.Classes().begin(), database.Classes().end());
    for (const auto& [name, values] : database.ClassParameters()) {
        classParameters_.insert_or_assign(name, values);
    }
    for (const auto& [name, values] : database.ClassProperties()) {
        classProperties_.insert_or_assign(name, values);
    }
}

void AISemanticActions::SetClassDuplicateCheck(bool enabled) noexcept { classDuplicateCheck_ = enabled; }

void AISemanticActions::SetHandlerWords(std::span<const std::u16string> words) {
    handlerWords_.assign(words.begin(), words.end());
}

void AISemanticActions::SetLocalVariables(std::span<const std::u16string> variables) {
    localVariables_.assign(variables.begin(), variables.end());
}

void AISemanticActions::DefineProperty(std::u16string name, const ScriptTypeDescriptor* type) {
    if (type == nullptr) {
        throw std::invalid_argument("property type cannot be null");
    }
    properties_.insert_or_assign(std::move(name), type);
}

void AISemanticActions::DefineParameter(std::u16string name, const ScriptTypeDescriptor* type) {
    if (type == nullptr) {
        throw std::invalid_argument("parameter type cannot be null");
    }
    parameters_.insert_or_assign(std::move(name), type);
}

void AISemanticActions::DefineArgumentAlias(std::u16string alias, std::u16string variable) {
    argumentAliases_.insert_or_assign(std::move(alias), std::move(variable));
}

void AISemanticActions::DefineVariableAlias(std::u16string alias, std::u16string object, std::u16string variable) {
    variableAliases_.insert_or_assign(std::move(alias), AIVariableAlias{std::move(object), std::move(variable)});
}

void AISemanticActions::ForbidAliasVariable(std::u16string name) { forbiddenAliasVariables_.insert(std::move(name)); }

std::span<const std::u16string> AISemanticActions::Diagnostics() const noexcept { return diagnostics_; }

std::span<const std::u16string> AISemanticActions::IncludeFiles() const noexcept { return includeFiles_; }

std::u16string_view AISemanticActions::SymbolOutput() const noexcept { return symbolOutput_; }

std::int32_t AISemanticActions::LoopCount() const noexcept { return loopCount_; }

AISemanticValue AISemanticActions::Execute(
    std::int16_t action, std::span<const AISemanticValue> stack, std::int16_t reductionLength
) {
    if (!Implements(action)) {
        throw std::logic_error("semantic action is not implemented in this group");
    }

    AISemanticValue result = DefaultValue(stack, reductionLength);
    const ScriptTypeDescriptor* const intType = FindScriptType(2);
    const ScriptTypeDescriptor* const floatType = FindScriptType(3);
    const ScriptTypeDescriptor* const stringType = FindScriptType(4);
    const ScriptTypeDescriptor* const doubleType = FindScriptType(5);
    const ScriptTypeDescriptor* const int64Type = FindScriptType(6);
    const ScriptTypeDescriptor* const pointerType = FindScriptType(7);

    switch (action) {
        case 0: {
            // Only record the dependency here. The compiler builds the missing module
            // first and then compiles this file again.
            std::u16string include(ValueText(FromTop(stack, 1)));
            include.append(u".ai");
            includeFiles_.push_back(std::move(include));
            break;
        }
        case 1: {
            className_.assign(ValueText(FromTop(stack, 1)));
            const AISemanticValue& superValue = FromTop(stack, 0);
            if (superValue.kind == AISemanticKind::Empty) {
                superClassName_.reset();
            } else {
                superClassName_ = ValueText(superValue);
            }
            if (superClassName_ && !knownClasses_.contains({eventTypeId_, *superClassName_})) {
                Error(BracketedNameError(u"Undefined super class [", *superClassName_));
            }
            codeGenerator_.NewClass(
                classKind_, eventTypeId_, className_,
                superClassName_ ? OptionalText{std::u16string_view(*superClassName_)} : std::nullopt
            );
            const bool duplicate = knownClasses_.contains({eventTypeId_, className_});
            knownClasses_.emplace(eventTypeId_, className_);
            if (classDuplicateCheck_ && duplicate) {
                Error(BracketedNameError(u"Duplicated class[", className_) + u" definition");
            }
            variableAliases_.clear();
            argumentAliases_.clear();
            constants_.clear();
            parameters_.clear();
            properties_.clear();
            eventHandlers_.clear();
            symbolOutput_.append(u"class : ");
            symbolOutput_.append(className_);
            symbolOutput_.append(u"\r\n");
            break;
        }
        case 2: {
            parameters_ = constants_;
            if (superClassName_) {
                const auto inherited = classParameters_.find(*superClassName_);
                if (inherited != classParameters_.end()) {
                    for (const auto& [name, type] : inherited->second) {
                        // Inherited parameters do not replace parameters of the derived class.
                        parameters_.try_emplace(name, type);
                    }
                }
            }
            AppendSymbolData(symbolOutput_, u"parameter : ", parameters_);
            classParameters_.insert_or_assign(className_, parameters_);
            constants_.clear();
            break;
        }
        case 3: {
            properties_ = constants_;
            if (superClassName_) {
                const auto inherited = classProperties_.find(*superClassName_);
                if (inherited != classProperties_.end()) {
                    for (const auto& [name, type] : inherited->second) {
                        // Unlike parameters, inherited properties replace the properties of
                        // the derived class.
                        properties_.insert_or_assign(name, type);
                    }
                }
            }
            AppendSymbolData(symbolOutput_, u"property : ", properties_);
            classProperties_.insert_or_assign(className_, properties_);
            constants_.clear();
            break;
        }
        case 4:
            codeGenerator_.ClassEnd();
            break;
        case 5:
            // This setter does nothing in the original.
            break;
        case 6: {
            const std::int64_t id = ValueInteger(FromTop(stack, 1));
            if (id < 0 || id > 1) {
                Error(u"Invalid base_event_type");
            } else {
                eventTypeId_ = static_cast<std::int32_t>(id);
            }
            break;
        }
        case 7:
            result = FromTop(stack, 0);
            break;
        case 8:
            result = {};
            break;
        case 9:
            handlerWords_.emplace_back(ValueText(FromTop(stack, 0)));
            break;
        case 10:
        case 11: {
            const std::u16string_view open = ValueText(FromTop(stack, 2));
            const std::u16string_view close = ValueText(FromTop(stack, 0));
            if (open != u"<" || close != u">") {
                Error(u"Invalid argument error");
                break;
            }
            const std::u16string_view alias = ValueText(FromTop(stack, 3));
            const std::u16string_view variable = ValueText(FromTop(stack, 1));
            if (variableAliases_.contains(alias)) {
                Error(BracketedNameError(u"Duplicated Alias [", alias) + u" in Class");
            }
            if (argumentAliases_.contains(alias)) {
                Error(BracketedNameError(u"Duplicated Alias [", alias) + u" in EventHandler");
                break;
            }
            argumentAliases_.emplace(alias, variable);
            handlerWords_.emplace_back(variable);
            forbiddenAliasVariables_.emplace(variable);
            break;
        }
        case 12:
            Error(u"missing ','\n");
            handlerWords_.emplace_back(ValueText(FromTop(stack, 0)));
            break;
        case 13:
            handlerWords_.clear();
            argumentAliases_.clear();
            forbiddenAliasVariables_.clear();
            handlerWords_.emplace_back(ValueText(FromTop(stack, 0)));
            break;
        case 14:
            argumentAliases_.clear();
            forbiddenAliasVariables_.clear();
            handlerWords_.clear();
            break;
        case 15: {
            const std::u16string_view object = ValueText(FromTop(stack, 4));
            const std::u16string_view variable = ValueText(FromTop(stack, 2));
            const std::u16string_view alias = ValueText(FromTop(stack, 1));
            if (!variableAliases_.emplace(alias, AIVariableAlias{std::u16string(object), std::u16string(variable)}).second) {
                Error(BracketedNameError(u"Duplicated Alias [", alias) + u" in Class");
            }
            break;
        }
        case 16: {
            const std::u16string_view variable = ValueText(FromTop(stack, 2));
            const std::u16string_view alias = ValueText(FromTop(stack, 1));
            if (!variableAliases_.emplace(alias, AIVariableAlias{{}, std::u16string(variable)}).second) {
                Error(BracketedNameError(u"Duplicated Alias [", alias) + u" in Class");
            }
            break;
        }
        case 17:
            codeGenerator_.ParameterDefineBegin();
            break;
        case 18:
            codeGenerator_.ParameterDefineEnd();
            break;
        case 19:
        case 27: {
            const std::u16string_view name = ValueText(FromTop(stack, 3));
            constants_.insert_or_assign(std::u16string(name), int64Type);
            codeGenerator_.IntType(name, ValueInteger(FromTop(stack, 1)));
            break;
        }
        case 20:
        case 28: {
            const std::u16string_view name = ValueText(FromTop(stack, 3));
            constants_.insert_or_assign(std::u16string(name), floatType);
            codeGenerator_.FloatType(name, ValueFloat(FromTop(stack, 1)));
            break;
        }
        case 21:
        case 29: {
            const std::u16string_view name = ValueText(FromTop(stack, 3));
            constants_.insert_or_assign(std::u16string(name), floatType);
            codeGenerator_.FloatType(name, static_cast<float>(ValueInteger(FromTop(stack, 1))));
            break;
        }
        case 22:
        case 30: {
            const std::u16string_view name = ValueText(FromTop(stack, 3));
            constants_.insert_or_assign(std::u16string(name), stringType);
            codeGenerator_.StringType(name, ValueText(FromTop(stack, 1)));
            break;
        }
        case 23: {
            const std::u16string_view name = ValueText(FromTop(stack, 1));
            codeGenerator_.WaypointsType(name);
            constants_.insert_or_assign(std::u16string(name), FindScriptType(27));
            break;
        }
        case 24: {
            const std::u16string_view name = ValueText(FromTop(stack, 1));
            codeGenerator_.WaypointDelaysType(name);
            constants_.insert_or_assign(std::u16string(name), FindScriptType(28));
            break;
        }
        case 25:
            codeGenerator_.PropertyDefineBegin();
            break;
        case 26:
            codeGenerator_.PropertyDefineEnd();
            break;
        case 31:
            codeGenerator_.TelposListBegin(ValueText(FromTop(stack, 0)));
            break;
        case 32: {
            const std::u16string_view name = ValueText(FromTop(stack, 6));
            constants_.insert_or_assign(std::u16string(name), FindScriptType(29));
            codeGenerator_.TelposListEnd();
            break;
        }
        case 33:
            codeGenerator_.BuysellListBegin(ValueText(FromTop(stack, 0)));
            break;
        case 34: {
            const std::u16string_view name = ValueText(FromTop(stack, 6));
            constants_.insert_or_assign(std::u16string(name), FindScriptType(30));
            codeGenerator_.BuysellListEnd();
            break;
        }
        case 35:
            codeGenerator_.Telpos2(
                LowI32(ValueInteger(FromTop(stack, 11))), LowI32(ValueInteger(FromTop(stack, 9))),
                LowI32(ValueInteger(FromTop(stack, 7))), LowI32(ValueInteger(FromTop(stack, 5))),
                LowI32(ValueInteger(FromTop(stack, 3))), LowI32(ValueInteger(FromTop(stack, 1)))
            );
            break;
        case 36:
            codeGenerator_.Telpos2(
                LowI32(ValueInteger(FromTop(stack, 9))), LowI32(ValueInteger(FromTop(stack, 7))),
                LowI32(ValueInteger(FromTop(stack, 5))), LowI32(ValueInteger(FromTop(stack, 3))),
                LowI32(ValueInteger(FromTop(stack, 1))), 0
            );
            break;
        case 37: {
            const AISemanticValue& list = FromTop(stack, 2);
            if (list.kind != AISemanticKind::IntegerList) {
                throw std::logic_error("yyattribute is not an integer vector");
            }
            codeGenerator_.TelposEx2(
                LowI32(ValueInteger(FromTop(stack, 13))), LowI32(ValueInteger(FromTop(stack, 11))),
                LowI32(ValueInteger(FromTop(stack, 9))), LowI32(ValueInteger(FromTop(stack, 7))),
                LowI32(ValueInteger(FromTop(stack, 5))), list.integerList
            );
            break;
        }
        case 38: {
            const AISemanticValue& previous = FromTop(stack, 2);
            if (previous.kind != AISemanticKind::IntegerList) {
                throw std::logic_error("yyattribute is not an integer vector");
            }
            std::vector<std::int32_t> values = previous.integerList;
            values.push_back(LowI32(ValueInteger(FromTop(stack, 0))));
            result = AISemanticValue::FromIntegerList(std::move(values));
            break;
        }
        case 39:
            result = AISemanticValue::FromIntegerList({LowI32(ValueInteger(FromTop(stack, 0)))});
            break;
        case 40:
        case 41:
        case 42: {
            const std::size_t nameOffset = action == 40 ? 3U : action == 41 ? 7U : 0U;
            const std::u16string_view name = ValueText(FromTop(stack, nameOffset));
            const auto symbol = precompiledSymbols_.find(name);
            const std::int32_t itemId = symbol == precompiledSymbols_.end() ? -1 : LowI32(symbol->second);
            if (symbol == precompiledSymbols_.end()) {
                std::u16string message(u"Invalid item name[");
                message.append(name);
                message.append(
                    action == 41 ? u"], reference ..\\Script\\Itemdata.txt" : u"], reference ..\\Script\\Itemdata.txt!"
                );
                Error(message);
            }
            if (action == 40) {
                codeGenerator_.Buysell(itemId, ValueInteger(FromTop(stack, 1)), 0.0F, 0);
            } else if (action == 41) {
                codeGenerator_.Buysell(
                    itemId, ValueInteger(FromTop(stack, 5)), ValueFloat(FromTop(stack, 3)), ValueInteger(FromTop(stack, 1))
                );
            } else {
                codeGenerator_.Buy(itemId, 0);
            }
            break;
        }
        case 43:
            break;
        case 44:
            handlerWords_.clear();
            localVariables_.clear();
            codeGenerator_.InitHandler();
            break;
        case 45: {
            const std::u16string_view name = ValueText(FromTop(stack, 4));
            const ScriptEventNameDescriptor* const event = FindScriptEventName(eventTypeId_, name);
            const std::int32_t eventId = event == nullptr ? -1 : event->eventId;
            if (event == nullptr) {
                Error(UndefinedEvent(name));
            }
            if (!eventHandlers_.insert(eventId).second) {
                Error(DuplicatedEvent(name, className_));
            }
            codeGenerator_.SetHandler(eventId, name);
            localVariables_ = handlerWords_;
            localVariables_.emplace_back(u"myself");
            localVariables_.emplace_back(u"_choiceN");
            localVariables_.emplace_back(u"_code");
            localVariables_.emplace_back(u"_from_choice");
            codeGenerator_.SetLocalVariables(localVariables_);
            break;
        }
        case 46:
            argumentAliases_.clear();
            codeGenerator_.FlushHandler();
            break;
        case 47:
            Error(u"EventHander (Undefined Event ID)\n");
            break;
        case 48:
        case 49:
        case 50:
        case 52:
        case 53:
        case 56:
            break;
        case 51:
            codeGenerator_.ShiftSp(-1);
            break;
        case 54:
            codeGenerator_.ExitHandler();
            break;
        case 55:
            codeGenerator_.CallSuper();
            break;
        case 57: {
            const std::int32_t label = codeGenerator_.PopIfLabel();
            codeGenerator_.WriteLabel(label);
            break;
        }
        case 58: {
            const std::int32_t next = codeGenerator_.NewCodeLabel();
            const std::int32_t previous = codeGenerator_.PopIfLabel();
            codeGenerator_.JumpLabel(next);
            codeGenerator_.WriteLabel(previous);
            codeGenerator_.PushIfLabel(next);
            break;
        }
        case 59: {
            const std::int32_t label = codeGenerator_.PopIfLabel();
            codeGenerator_.WriteLabel(label);
            break;
        }
        case 60: {
            const std::int32_t label = codeGenerator_.NewCodeLabel();
            codeGenerator_.WriteLabel(label);
            codeGenerator_.PushLabel(label);
            break;
        }
        case 61: {
            const ScriptTypeDescriptor* const condition = ValueType(FromTop(stack, 1));
            if (!IsIntegralType(condition)) {
                Error(u"WHILE (inttype)");
            }
            const std::int32_t end = codeGenerator_.NewCodeLabel();
            ++loopCount_;
            Flush(condition);
            codeGenerator_.BranchFalse(end);
            codeGenerator_.PushLabel(end);
            break;
        }
        case 62: {
            const std::int32_t end = codeGenerator_.PopLabel();
            const std::int32_t begin = codeGenerator_.PopLabel();
            codeGenerator_.JumpLabel(begin);
            codeGenerator_.WriteLabel(end);
            --loopCount_;
            break;
        }
        case 63: {
            const ScriptTypeDescriptor* const condition = ValueType(FromTop(stack, 1));
            if (!IsIntegralType(condition)) {
                Error(u"select ([inttype])");
            }
            codeGenerator_.PushLabel(codeGenerator_.NewCodeLabel());
            Flush(condition);
            ++loopCount_;
            codeGenerator_.PushSelectLabel(codeGenerator_.NewCodeLabel());
            break;
        }
        case 64: {
            codeGenerator_.WriteLabel(codeGenerator_.PopSelectLabel());
            codeGenerator_.WriteLabel(codeGenerator_.PopLabel());
            codeGenerator_.ShiftSp(-1);
            --loopCount_;
            break;
        }
        case 65:
            forLabelA_ = codeGenerator_.NewCodeLabel();
            forLabelB_ = codeGenerator_.NewCodeLabel();
            forLabelC_ = codeGenerator_.NewCodeLabel();
            forLabelD_ = codeGenerator_.NewCodeLabel();
            codeGenerator_.ShiftSp(-1);
            codeGenerator_.WriteLabel(forLabelA_);
            codeGenerator_.PushLabel(forLabelB_);
            codeGenerator_.PushLabel(forLabelD_);
            break;
        case 66: {
            const ScriptTypeDescriptor* const condition = ValueType(FromTop(stack, 1));
            if (!IsIntegralType(condition)) {
                Error(u"FOR(expr_e; expr_e; expr_e[inttype])");
            }
            codeGenerator_.BranchFalse(forLabelD_);
            codeGenerator_.JumpLabel(forLabelC_);
            codeGenerator_.WriteLabel(forLabelB_);
            break;
        }
        case 67:
            ++loopCount_;
            codeGenerator_.ShiftSp(-1);
            codeGenerator_.JumpLabel(forLabelA_);
            codeGenerator_.WriteLabel(forLabelC_);
            break;
        case 68: {
            const std::int32_t end = codeGenerator_.PopLabel();
            const std::int32_t iteration = codeGenerator_.PopLabel();
            --loopCount_;
            codeGenerator_.JumpLabel(iteration);
            codeGenerator_.WriteLabel(end);
            break;
        }
        case 69:
            if (loopCount_ == 0) {
                Error(u"illegal break. there is no loop.");
            } else {
                const std::int32_t end = codeGenerator_.PopLabel();
                codeGenerator_.PushLabel(end);
                codeGenerator_.JumpLabel(end);
            }
            break;
        case 70: {
            if (loopCount_ == 0) {
                Error(u"illegal continue. there is no loop.");
            }
            const std::int32_t end = codeGenerator_.PopLabel();
            const std::int32_t begin = codeGenerator_.PopLabel();
            codeGenerator_.JumpLabel(begin);
            codeGenerator_.PushLabel(begin);
            codeGenerator_.PushLabel(end);
            break;
        }
        case 71: {
            const std::int32_t begin = codeGenerator_.NewCodeLabel();
            const std::int32_t end = codeGenerator_.NewCodeLabel();
            Flush(ValueType(FromTop(stack, 0)));
            codeGenerator_.BranchFalse(end);
            codeGenerator_.WriteLabel(begin);
            codeGenerator_.PushIfLabel(end);
            break;
        }
        case 72: {
            const std::int32_t previous = codeGenerator_.PopSelectLabel();
            const std::int32_t next = codeGenerator_.NewCodeLabel();
            codeGenerator_.PushSelectLabel(next);
            codeGenerator_.PushRegisterSp();
            codeGenerator_.Fetch(pointerType, u"select_case compare");
            codeGenerator_.PushConst(ValueInteger(FromTop(stack, 1)), u"select_case");
            codeGenerator_.Equal();
            codeGenerator_.BranchFalse(next);
            codeGenerator_.WriteLabel(previous);
            break;
        }
        case 73: {
            const std::int32_t previous = codeGenerator_.PopSelectLabel();
            const std::int32_t next = codeGenerator_.NewCodeLabel();
            codeGenerator_.PushSelectLabel(next);
            codeGenerator_.JumpLabel(next);
            codeGenerator_.WriteLabel(previous);
            break;
        }
        case 74:
            Error(u"case [int_const]:");
            break;
        case 75:
            Error(u"case int_const[:]");
            break;
        case 76:
            Flush(ValueType(FromTop(stack, 0)));
            break;
        case 77:
            break;
        case 78:
            if (fetchCount_ < 1) {
                Error(u"expr = expr: l-value is required.");
            } else {
                codeGenerator_.PushRegisterSp();
                codeGenerator_.Fetch(pointerType, u"expr =");
            }
            break;
        case 79: {
            const ScriptTypeDescriptor* const left = ValueType(FromTop(stack, 3));
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            result = AISemanticValue::FromType(left);
            if (!AssignCompatible(left, right)) {
                Error(u"a = b, assign incompatible");
            }
            Flush(right);
            if (left == intType || left == floatType) {
                codeGenerator_.Assign4();
            } else {
                codeGenerator_.Assign();
            }
            fetchCount_ = 1;
            break;
        }
        case 80:
            break;
        case 81: {
            const std::int32_t label = codeGenerator_.PopOrLabel();
            if (label != -1) {
                codeGenerator_.WriteLabel(label);
            }
            break;
        }
        case 82: {
            const ScriptTypeDescriptor* const left = ValueType(FromTop(stack, 1));
            if (IsIntegralType(left)) {
                Flush(left);
                std::int32_t label = codeGenerator_.PopOrLabel();
                if (label == -1) {
                    label = codeGenerator_.NewCodeLabel();
                }
                codeGenerator_.PushOrLabel(label);
                codeGenerator_.PushRegisterSp();
                codeGenerator_.Fetch(pointerType, u"or exit label");
                codeGenerator_.BranchTrue(label);
            } else {
                Error(u"float type is not allowed in OR list\n");
            }
            break;
        }
        case 83: {
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            if (IsIntegralType(right)) {
                Flush(right);
                codeGenerator_.LogicalOr();
            } else {
                Error(u"float type is not allowed in OR list\n");
            }
            break;
        }
        case 84:
            codeGenerator_.PushOrLabel(-1);
            break;
        case 85: {
            const std::int32_t label = codeGenerator_.PopAndLabel();
            if (label != -1) {
                codeGenerator_.WriteLabel(label);
            }
            break;
        }
        case 86: {
            const ScriptTypeDescriptor* const left = ValueType(FromTop(stack, 1));
            if (IsIntegralType(left)) {
                Flush(left);
                std::int32_t label = codeGenerator_.PopAndLabel();
                if (label == -1) {
                    label = codeGenerator_.NewCodeLabel();
                }
                codeGenerator_.PushAndLabel(label);
                codeGenerator_.PushRegisterSp();
                codeGenerator_.Fetch(pointerType, u"and list");
                codeGenerator_.BranchFalse(label);
            } else {
                Error(u"float type is not allowed in AND list\n");
            }
            break;
        }
        case 87: {
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            Flush(right);
            if (IsIntegralType(right)) {
                result = AISemanticValue::FromType(intType);
                // The original flushes the same operand a second time. The first
                // flush has already set the fetch count to zero.
                Flush(right);
                codeGenerator_.LogicalAnd();
            } else {
                Error(u"only inttype is allowed in AND list\n");
            }
            break;
        }
        case 88:
            codeGenerator_.PushAndLabel(-1);
            break;
        case 89:
        case 91:
        case 93:
        case 95:
        case 97:
        case 99:
        case 101:
        case 103:
        case 105:
        case 107:
            Flush(ValueType(FromTop(stack, 1)));
            break;
        case 90: {
            result = AISemanticValue::FromType(intType);
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            Flush(right);
            const std::u16string_view operation = ValueText(FromTop(stack, 2));
            if (operation == u"<") {
                codeGenerator_.Less();
            } else if (operation == u"<=" || operation == u"=<") {
                codeGenerator_.LessEqual();
            } else if (operation == u">=" || operation == u"=>") {
                codeGenerator_.GreaterEqual();
            } else if (operation == u">") {
                codeGenerator_.Greater();
            }
            break;
        }
        case 92: {
            const ScriptTypeDescriptor* const left = ValueType(FromTop(stack, 3));
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            if (OperatorCompatible(left, right)) {
                Flush(right);
                const std::u16string_view operation = ValueText(FromTop(stack, 2));
                if (operation == u"==") {
                    codeGenerator_.Equal();
                } else if (operation == u"!=") {
                    codeGenerator_.NotEqual();
                }
            } else {
                Error(u"== : different types between two operands");
            }
            result = AISemanticValue::FromType(intType);
            break;
        }
        case 94:
        case 96:
        case 98: {
            const ScriptTypeDescriptor* const left = ValueType(FromTop(stack, 3));
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            if (IsIntegralType(left) && IsIntegralType(right)) {
                Flush(right);
                if (action == 94) {
                    codeGenerator_.BitwiseXor();
                } else if (action == 96) {
                    codeGenerator_.BitwiseAnd();
                } else {
                    codeGenerator_.BitwiseOr();
                }
            } else if (action == 94) {
                Error(u"^(xor operator) usage : int ^ int");
            } else if (action == 96) {
                Error(u"& (bitwise and operator) usage : int & int");
            } else {
                Error(u"| (bitwise or operator) usage : int | int");
            }
            result = AISemanticValue::FromType(left);
            break;
        }
        case 100:
        case 102:
        case 108: {
            const ScriptTypeDescriptor* const left = ValueType(FromTop(stack, 3));
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            const BinaryOperationTypeResult resolved = BinaryOperationType(left, right);
            if (!resolved.compatible) {
                Error(
                    left->isClass || right->isClass ? u"pointer is not allowed to binary_op " : u"invalid operands to binary"
                );
            }
            result = AISemanticValue::FromType(resolved.type);
            Flush(right);
            if (action == 100) {
                codeGenerator_.Multiply();
            } else if (action == 102) {
                codeGenerator_.Divide();
            } else {
                codeGenerator_.Subtract();
            }
            break;
        }
        case 104: {
            const ScriptTypeDescriptor* const left = ValueType(FromTop(stack, 3));
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            if (!IsIntegralType(left) || !IsIntegralType(right)) {
                Error(u"modular operation : int % int");
            }
            result = AISemanticValue::FromType(intType);
            Flush(right);
            codeGenerator_.Modulo();
            break;
        }
        case 106: {
            const ScriptTypeDescriptor* const left = ValueType(FromTop(stack, 3));
            const ScriptTypeDescriptor* const right = ValueType(FromTop(stack, 0));
            if (left != stringType && right != stringType) {
                const BinaryOperationTypeResult resolved = BinaryOperationType(left, right);
                if (!resolved.compatible) {
                    Error(
                        left->isClass || right->isClass ? u"pointer is not allowed to binary_op "
                                                        : u"invalid operands to binary"
                    );
                }
                result = AISemanticValue::FromType(resolved.type);
                Flush(right);
                codeGenerator_.Add();
            } else {
                if (IsStringAddOperand(left) && IsStringAddOperand(right)) {
                    Flush(right);
                    codeGenerator_.AddString(left, right);
                } else {
                    Error(u"operator + : incompatible types");
                }
                result = AISemanticValue::FromType(stringType);
            }
            break;
        }
        case 109:
            break;
        case 110:
            result = FromTop(stack, 1);
            break;
        case 111:
            if (codeGenerator_.DummyStackPoint() < 0) {
                Error(u"Stack underflow may be occurred by unary constant");
            }
            result = AISemanticValue::FromType(int64Type);
            codeGenerator_.PushConst(ValueInteger(FromTop(stack, 0)), u"unary->INTEGER_CONST");
            fetchCount_ = 0;
            break;
        case 112:
            if (codeGenerator_.DummyStackPoint() < 0) {
                Error(u"Stack underflow may be occurred by unary constant");
            }
            result = AISemanticValue::FromType(floatType);
            codeGenerator_.PushConstFloat(ValueFloat(FromTop(stack, 0)), u"unary->FLOAT_CONST");
            fetchCount_ = 0;
            break;
        case 113: {
            const std::int32_t label = codeGenerator_.DeclareString(ValueText(FromTop(stack, 0)));
            codeGenerator_.PushStringLabel(label);
            fetchCount_ = 0;
            result = AISemanticValue::FromType(stringType);
            break;
        }
        case 114: {
            const ScriptTypeDescriptor* const operand = ValueType(FromTop(stack, 0));
            if (operand != intType && operand != floatType && operand != doubleType && operand != int64Type) {
                Error(u"-(inttype | floattype) is allowed");
                result = AISemanticValue::FromType(intType);
            } else {
                Flush(operand);
                codeGenerator_.Negate();
                result = AISemanticValue::FromType(operand);
            }
            break;
        }
        case 115: {
            const ScriptTypeDescriptor* const operand = ValueType(FromTop(stack, 0));
            if (IsIntegralType(operand)) {
                Flush(operand);
                codeGenerator_.LogicalNot();
            } else {
                Error(u"! (not operator) : ! inttype");
            }
            result = AISemanticValue::FromType(operand);
            break;
        }
        case 116: {
            const std::u16string_view originalName = ValueText(FromTop(stack, 0));
            std::u16string resolvedName(originalName);
            const ScriptVariableDescriptor* eventVariable = nullptr;

            const auto variableAlias = variableAliases_.find(originalName);
            if (variableAlias != variableAliases_.end()) {
                const AIVariableAlias& alias = variableAlias->second;
                resolvedName = alias.variable;
                if (alias.object.empty()) {
                    eventVariable = FindScriptEventVariable(eventTypeId_, resolvedName);
                } else {
                    const ScriptVariableDescriptor* const object = FindScriptEventVariable(eventTypeId_, alias.object);
                    if (object != nullptr) {
                        codeGenerator_.PushEvent(alias.object);
                        codeGenerator_.PushConst(object->offset, alias.object);
                        codeGenerator_.Add();
                        const ScriptVariableDescriptor* const member = FindScriptVariable(object->typeId, resolvedName);
                        if (member != nullptr) {
                            codeGenerator_.Fetch(pointerType, resolvedName);
                            codeGenerator_.PushConst(member->offset, resolvedName);
                            codeGenerator_.Add();
                            const ScriptTypeDescriptor* const memberType = FindScriptType(member->typeId);
                            result = AISemanticValue::FromType(memberType);
                            fetchCount_ = memberType == stringType ? 0 : 1;
                            break;
                        }
                        Error(MissingAliasedField(alias.object, resolvedName));
                        result = AISemanticValue::FromType(intType);
                        fetchCount_ = 0;
                        break;
                    }
                }
            } else {
                const auto argumentAlias = argumentAliases_.find(originalName);
                if (argumentAlias != argumentAliases_.end()) {
                    resolvedName = argumentAlias->second;
                    eventVariable = FindScriptEventVariable(eventTypeId_, resolvedName);
                } else {
                    if (!argumentAliases_.empty() && forbiddenAliasVariables_.contains(originalName)) {
                        Error(ForbiddenAliasVariable(originalName));
                    }
                    eventVariable = FindScriptEventVariable(eventTypeId_, resolvedName);
                }
            }

            if (eventVariable == nullptr) {
                const auto property = properties_.find(resolvedName);
                if (property != properties_.end()) {
                    result = AISemanticValue::FromType(property->second);
                    codeGenerator_.PushProperty(resolvedName);
                    fetchCount_ = 0;
                    break;
                }
                const auto parameter = parameters_.find(resolvedName);
                if (parameter != parameters_.end()) {
                    result = AISemanticValue::FromType(parameter->second);
                    codeGenerator_.PushParameter(resolvedName);
                    fetchCount_ = 0;
                    break;
                }
                Error(BracketedNameError(u"Undefined variable [", resolvedName));
                result = AISemanticValue::FromType(intType);
                break;
            }

            bool isLocal = false;
            for (const std::u16string& local : localVariables_) {
                if (local == resolvedName) {
                    isLocal = true;
                    break;
                }
            }
            if (isLocal) {
                codeGenerator_.PushEvent(resolvedName);
                codeGenerator_.PushConst(eventVariable->offset, resolvedName);
                codeGenerator_.Add();
                fetchCount_ = 1;
                result = AISemanticValue::FromType(FindScriptType(eventVariable->typeId));
            } else {
                Error(BracketedNameError(u"Undeclared variable [", resolvedName));
                fetchCount_ = 1;
                result = AISemanticValue::FromType(intType);
            }
            break;
        }
        case 117: {
            const AISemanticValue& baseValue = FromTop(stack, 2);
            if (baseValue.kind != AISemanticKind::ScriptType) {
                throw std::logic_error("yyattribute is not a ScriptTypeInfo pointer");
            }
            const ScriptTypeDescriptor* const base = baseValue.scriptType;
            std::u16string resolvedName(ValueText(FromTop(stack, 0)));
            if (base == nullptr) {
                Error(u"something wrong2 -_-;;;");
                result = AISemanticValue::FromType(intType);
                fetchCount_ = 0;
                break;
            }
            if (!base->isClass) {
                Error(u"not a class type before '.' operator");
                result = AISemanticValue::FromType(intType);
                fetchCount_ = 0;
                break;
            }

            const auto argumentAlias = argumentAliases_.find(resolvedName);
            if (argumentAlias != argumentAliases_.end()) {
                resolvedName = argumentAlias->second;
            } else {
                const auto variableAlias = variableAliases_.find(resolvedName);
                if (variableAlias != variableAliases_.end()) {
                    resolvedName = variableAlias->second.variable;
                } else if (!argumentAliases_.empty() && forbiddenAliasVariables_.contains(resolvedName)) {
                    Error(ForbiddenAliasVariable(resolvedName));
                }
            }

            const ScriptVariableDescriptor* const member = FindScriptVariable(base->id, resolvedName);
            if (member == nullptr) {
                Error(MissingField(resolvedName));
                result = AISemanticValue::FromType(intType);
                fetchCount_ = 0;
                break;
            }
            codeGenerator_.Fetch(pointerType, resolvedName);
            codeGenerator_.PushConst(member->offset, resolvedName);
            codeGenerator_.Add();
            const ScriptTypeDescriptor* const memberType = FindScriptType(member->typeId);
            result = AISemanticValue::FromType(memberType);
            fetchCount_ = memberType == stringType ? 0 : 1;
            break;
        }
        case 118: {
            const std::u16string_view name = ValueText(FromTop(stack, 1));
            const ScriptVariableDescriptor* receiver = FindScriptEventVariable(eventTypeId_, u"myself");
            if (receiver == nullptr) {
                throw std::logic_error("event metadata has no myself variable");
            }
            if (FindScriptFunction(receiver->typeId, name) == nullptr) {
                receiver = FindScriptEventVariable(eventTypeId_, u"gg");
            }
            if (receiver == nullptr) {
                throw std::logic_error("event metadata has no gg variable");
            }
            codeGenerator_.PushEvent(receiver->name);
            codeGenerator_.PushConst(receiver->offset, name);
            codeGenerator_.Add();
            codeGenerator_.Fetch(FindScriptType(receiver->typeId), name);
            break;
        }
        case 119:
        case 123: {
            const bool memberCall = action == 123;
            const std::size_t ownerOffset = memberCall ? 6U : 0U;
            const std::size_t nameOffset = memberCall ? 4U : 4U;
            const ScriptTypeDescriptor* owner = nullptr;
            const ScriptFunctionDescriptor* function = nullptr;
            const ScriptVariableDescriptor* receiver = nullptr;
            const std::u16string_view name = ValueText(FromTop(stack, nameOffset));

            if (memberCall) {
                owner = ValueType(FromTop(stack, ownerOffset));
                function = FindScriptFunction(owner->id, name);
            } else {
                receiver = FindScriptEventVariable(eventTypeId_, u"myself");
                if (receiver == nullptr) {
                    throw std::logic_error("event metadata has no myself variable");
                }
                function = FindScriptFunction(receiver->typeId, name);
                if (function == nullptr) {
                    receiver = FindScriptEventVariable(eventTypeId_, u"gg");
                    if (receiver == nullptr) {
                        throw std::logic_error("event metadata has no gg variable");
                    }
                    function = FindScriptFunction(receiver->typeId, name);
                }
                owner = FindScriptType(receiver->typeId);
            }

            if (function == nullptr) {
                Error(UndefinedFunction(name));
                fetchCount_ = 0;
                result = AISemanticValue::FromType(intType);
                break;
            }

            const AISemanticValue& argumentsValue = FromTop(stack, 1);
            if (argumentsValue.kind != AISemanticKind::TypeList) {
                throw std::logic_error("yyattribute is not an argument type vector");
            }
            const std::span<const std::int32_t> expected = ArgumentTypes(*function);
            const std::vector<const ScriptTypeDescriptor*>& actual = argumentsValue.typeList;
            bool compatible = expected.size() == actual.size();
            if (!compatible) {
                Error(ParameterCountError(name, expected.size(), actual.size()));
            } else {
                for (std::size_t index = 0; index < actual.size(); ++index) {
                    const ScriptTypeDescriptor* const expectedType = FindScriptType(expected[index]);
                    if (!AssignCompatible(expectedType, actual[index])) {
                        compatible = false;
                        Error(IncompatibleParameter(name, index, memberCall));
                    }
                }
            }

            if (compatible) {
                // For implicit calls the executable encodes the selected event
                // receiver's type. For member calls it encodes the base type.
                if (owner == nullptr || owner->id != function->ownerTypeId) {
                    throw std::logic_error("function owner metadata mismatch");
                }
                codeGenerator_.FunctionCall(EncodedFunctionIndex(*function), name);
                codeGenerator_.ShiftSp(-static_cast<std::int32_t>(actual.size()));
                result = AISemanticValue::FromType(FindScriptType(function->returnTypeId));
            } else {
                result = AISemanticValue::FromType(intType);
            }
            fetchCount_ = 0;
            break;
        }
        case 120:
        case 121: {
            const bool memberCall = action == 121;
            const std::u16string_view name = ValueText(FromTop(stack, 2));
            const ScriptTypeDescriptor* owner = nullptr;
            const ScriptFunctionDescriptor* function = nullptr;

            if (memberCall) {
                owner = ValueType(FromTop(stack, 4));
                codeGenerator_.Fetch(owner, u"Fetch pointer");
                function = FindScriptFunction(owner->id, name);
            } else {
                const ScriptVariableDescriptor* receiver = FindScriptEventVariable(eventTypeId_, u"myself");
                if (receiver == nullptr) {
                    throw std::logic_error("event metadata has no myself variable");
                }
                function = FindScriptFunction(receiver->typeId, name);
                if (function == nullptr) {
                    receiver = FindScriptEventVariable(eventTypeId_, u"gg");
                }
                if (receiver == nullptr) {
                    throw std::logic_error("event metadata has no gg variable");
                }
                owner = FindScriptType(receiver->typeId);
                function = FindScriptFunction(owner->id, name);

                codeGenerator_.PushEvent(receiver->name);
                codeGenerator_.PushConst(receiver->offset, name);
                codeGenerator_.Add();
                codeGenerator_.Fetch(owner, name);
            }

            if (function == nullptr) {
                Error(UndefinedFunction(name));
                result = AISemanticValue::FromType(intType);
            } else if (function->argumentCount == 0) {
                if (owner == nullptr || owner->id != function->ownerTypeId) {
                    throw std::logic_error("function owner metadata mismatch");
                }
                codeGenerator_.FunctionCall(EncodedFunctionIndex(*function), name);
                result = AISemanticValue::FromType(FindScriptType(function->returnTypeId));
            } else {
                Error(ParameterCountError(name, function->argumentCount, 0));
                result = AISemanticValue::FromType(intType);
            }
            fetchCount_ = 0;
            break;
        }
        case 122:
            codeGenerator_.Fetch(ValueType(FromTop(stack, 3)), u"Fetch Base pointer");
            break;
        case 124:
        case 125: {
            const bool decrement = action == 124;
            const ScriptTypeDescriptor* const operand = ValueType(FromTop(stack, 0));
            if (!IsIntegralType(operand)) {
                Error(
                    decrement ? u"-- operatoer can be operated with only integer type."
                              : u"++ operatoer can be operated with only integer type."
                );
                result = AISemanticValue::FromType(intType);
                fetchCount_ = 0;
                break;
            }
            result = AISemanticValue::FromType(operand);
            if (fetchCount_ == 0) {
                Error(decrement ? u"-- unary : unary is not a l-value" : u"++ unary : unary is not a l-value");
                break;
            }
            const std::u16string_view marker = decrement ? u"--" : u"++";
            codeGenerator_.PushRegisterSp();
            codeGenerator_.Fetch(pointerType, marker);
            codeGenerator_.PushRegisterSp();
            codeGenerator_.Fetch(pointerType, marker);
            codeGenerator_.Fetch(operand);
            codeGenerator_.PushConst(1, marker);
            if (decrement) {
                codeGenerator_.Subtract();
            } else {
                codeGenerator_.Add();
            }
            if (operand == intType) {
                codeGenerator_.Assign4();
            } else {
                codeGenerator_.Assign();
            }
            codeGenerator_.Fetch(operand, decrement ? OptionalText{u"after --"} : OptionalText{u"After assign"});
            fetchCount_ = 0;
            break;
        }
        case 126: {
            const ScriptTypeDescriptor* const type = ValueType(FromTop(stack, 0));
            result = AISemanticValue::FromTypeList({type});
            Flush(type);
            break;
        }
        case 127: {
            const AISemanticValue& previous = FromTop(stack, 2);
            if (previous.kind != AISemanticKind::TypeList) {
                throw std::logic_error("yyattribute is not an argument type vector");
            }
            std::vector<const ScriptTypeDescriptor*> types = previous.typeList;
            const ScriptTypeDescriptor* const type = ValueType(FromTop(stack, 0));
            types.push_back(type);
            result = AISemanticValue::FromTypeList(std::move(types));
            Flush(type);
            break;
        }
        case 128:
            result = FromTop(stack, 0);
            break;
        case 129: {
            // Negation wraps like the x64 NEG instruction, also for INT64_MIN. The
            // unsigned form avoids undefined behavior.
            const std::uint64_t bits = std::bit_cast<std::uint64_t>(ValueInteger(FromTop(stack, 0)));
            result = AISemanticValue::FromInteger(std::bit_cast<std::int64_t>(std::uint64_t{0} - bits));
            break;
        }
        default:
            throw std::logic_error("unreachable semantic action");
    }

    return result;
}

} // namespace nasc
