#include "asset/serialization/animation_controller_serializer.h"
#include "common/json.h"
#include <array>
#include <charconv>

namespace Comet {
    namespace {
        constexpr std::array<std::string_view, 6> OPERATORS{"==", "!=", ">", ">=", "<", "<="};
        template<typename T>
        Result<void> field(
            Json::Node node, const Json::Context& context, std::string_view key, T& target) {
            auto read = context.read_optional_field<T>(node, key, "a value of the declared type");
            if(!read)
                return Result<void>::failure(read.error());
            if(read.value())
                target = std::move(*read.value());
            return Result<void>::success();
        }
        Result<simdjson::dom::array> array(
            Json::Node node, std::string_view key, const Json::Context& context) {
            auto child = context.required_child(node, key);
            if(!child)
                return Result<simdjson::dom::array>::failure(child.error());
            return context.array(child.value(), key);
        }
        Result<ParameterValue> scalar(
            Json::Node node, const Json::Context& context, std::string_view key) {
            if(node.is_string()) {
                auto value = context.read_scalar<std::string>(node, key, "a string");
                if(!value)
                    return Result<ParameterValue>::failure(value.error());
                return Result<ParameterValue>::success(std::move(value).value());
            }
            if(node.is_bool()) {
                auto value = context.read_scalar<bool>(node, key, "a boolean");
                if(!value)
                    return Result<ParameterValue>::failure(value.error());
                return Result<ParameterValue>::success(value.value());
            }
            auto value = context.read_scalar<float>(node, key, "a finite number or boolean");
            if(!value)
                return Result<ParameterValue>::failure(value.error());
            return Result<ParameterValue>::success(value.value());
        }
        void write_scalar(Json::Writer& writer, const ParameterValue& value) {
            if(const auto* boolean = std::get_if<bool>(&value))
                writer.value(*boolean);
            else if(const auto* text = std::get_if<std::string>(&value))
                writer.value(*text);
            else
                writer.value(std::get<float>(value));
        }
        Result<AnimationController> decode(Json::Node root, const Json::Context& context) {
            using Read = Result<AnimationController>;
            AnimationController result;
            result.states.clear();
            auto keys = context.validate_keys(
                root, {"initial", "parameters", "states", "transitions", "library", "layers"});
            if(!keys)
                return Read::failure(keys.error());
            if(auto initial = field(root, context, "initial", result.initial); !initial)
                return Read::failure(initial.error());
            std::string library;
            if(auto read = field(root, context, "library", library); !read)
                return Read::failure(read.error());
            if(!library.empty()) {
                uint64_t handle = 0;
                const auto parsed =
                    std::from_chars(library.data(), library.data() + library.size(), handle);
                if(parsed.ec != std::errc{} || parsed.ptr != library.data() + library.size()
                    || !handle)
                    return Read::failure("Invalid animation library handle");
                result.library = AssetHandle(handle);
            }
            auto layer_node = context.find(root, "layers");
            if(!layer_node)
                return Read::failure(layer_node.error());
            if(layer_node.value()) {
                auto layers = context.array(*layer_node.value(), "layers");
                if(!layers)
                    return Read::failure(layers.error());
                for(const auto node : layers.value()) {
                    AnimationController::Layer layer;
                    for(auto read : {context.validate_keys(node,
                                         {"name", "state", "weight", "weight_parameter", "mask"}),
                            field(node, context, "name", layer.name),
                            field(node, context, "state", layer.state),
                            field(node, context, "weight", layer.weight),
                            field(node, context, "weight_parameter", layer.weight_parameter)})
                        if(!read)
                            return Read::failure(read.error());
                    auto mask = array(node, "mask", context);
                    if(!mask)
                        return Read::failure(mask.error());
                    for(const auto bone : mask.value()) {
                        auto name = context.read_scalar<std::string>(bone, "mask", "a node name");
                        if(!name)
                            return Read::failure(name.error());
                        layer.mask.push_back(std::move(name).value());
                    }
                    result.layers.push_back(std::move(layer));
                }
            }
            auto parameters_node = context.required_child(root, "parameters");
            if(!parameters_node)
                return Read::failure(parameters_node.error());
            auto parameters = context.object(parameters_node.value(), "parameters");
            if(!parameters)
                return Read::failure(parameters.error());
            for(const auto entry : parameters.value()) {
                auto value = scalar(entry.value, context, entry.key);
                if(!value)
                    return Read::failure(value.error());
                if(!result.parameters.emplace(std::string(entry.key), value.value()).second)
                    return Read::failure(context.error("parameters", "duplicate name"));
            }
            auto states = array(root, "states", context);
            if(!states)
                return Read::failure(states.error());
            for(const auto node : states.value()) {
                AnimationController::State state;
                state.motions.clear();
                for(auto read :
                    {context.validate_keys(node, {"name", "motions", "parameter", "smoothing",
                                                     "loop", "events", "parameter_y"}),
                        field(node, context, "name", state.name),
                        field(node, context, "parameter", state.parameter),
                        field(node, context, "parameter_y", state.parameter_y),
                        field(node, context, "smoothing", state.smoothing),
                        field(node, context, "loop", state.loop)})
                    if(!read)
                        return Read::failure(read.error());
                auto motions = array(node, "motions", context);
                if(!motions)
                    return Read::failure(motions.error());
                for(const auto motion : motions.value()) {
                    AnimationBlendPoint point{0, {}};
                    for(auto read : {context.validate_keys(motion, {"threshold", "clip", "y"}),
                            field(motion, context, "threshold", point.threshold),
                            field(motion, context, "y", point.y),
                            field(motion, context, "clip", point.clip)})
                        if(!read)
                            return Read::failure(read.error());
                    state.motions.push_back(std::move(point));
                }
                auto events_node = context.find(node, "events");
                if(!events_node)
                    return Read::failure(events_node.error());
                if(events_node.value()) {
                    auto events = context.array(*events_node.value(), "events");
                    if(!events)
                        return Read::failure(events.error());
                    for(const auto event_node : events.value()) {
                        AnimationController::Event event;
                        for(auto read :
                            {context.validate_keys(event_node, {"phase", "name", "value"}),
                                field(event_node, context, "phase", event.phase),
                                field(event_node, context, "name", event.name)})
                            if(!read)
                                return Read::failure(read.error());
                        auto value_node = context.required_child(event_node, "value");
                        if(!value_node)
                            return Read::failure(value_node.error());
                        auto value = scalar(value_node.value(), context, "value");
                        if(!value)
                            return Read::failure(value.error());
                        event.value = std::move(value).value();
                        state.events.push_back(std::move(event));
                    }
                }
                result.states.push_back(std::move(state));
            }
            auto transitions = array(root, "transitions", context);
            if(!transitions)
                return Read::failure(transitions.error());
            for(const auto node : transitions.value()) {
                AnimationController::Transition transition;
                for(auto read : {context.validate_keys(node,
                                     {"from", "to", "duration", "after_finished", "conditions"}),
                        field(node, context, "from", transition.from),
                        field(node, context, "to", transition.to),
                        field(node, context, "duration", transition.duration),
                        field(node, context, "after_finished", transition.after_finished)})
                    if(!read)
                        return Read::failure(read.error());
                auto conditions = array(node, "conditions", context);
                if(!conditions)
                    return Read::failure(conditions.error());
                for(const auto condition_node : conditions.value()) {
                    AnimationController::Condition condition;
                    std::string compare = "==";
                    for(auto read :
                        {context.validate_keys(condition_node, {"parameter", "compare", "value"}),
                            field(condition_node, context, "parameter", condition.parameter),
                            field(condition_node, context, "compare", compare)})
                        if(!read)
                            return Read::failure(read.error());
                    const auto op = std::ranges::find(OPERATORS, compare);
                    if(op == OPERATORS.end())
                        return Read::failure(context.error("compare", "unknown operator"));
                    condition.compare =
                        AnimationController::Condition::Compare(op - OPERATORS.begin());
                    auto child = context.required_child(condition_node, "value");
                    if(!child)
                        return Read::failure(child.error());
                    auto value = scalar(child.value(), context, condition.parameter);
                    if(!value)
                        return Read::failure(value.error());
                    condition.value = std::move(value).value();
                    transition.conditions.push_back(std::move(condition));
                }
                result.transitions.push_back(std::move(transition));
            }
            if(auto valid = result.validate(); !valid)
                return Read::failure(context.error("<root>", valid.error()));
            return Read::success(std::move(result));
        }
    }
    Result<AnimationController> AnimationControllerSerializer::deserialize(
        std::string_view contents, std::string_view source) const {
        return Json::deserialize<AnimationController>(
            "animation controller", contents, source, decode);
    }
    Result<std::string> AnimationControllerSerializer::serialize(
        const AnimationController& data) const {
        if(auto valid = data.validate(); !valid)
            return Result<std::string>::failure(valid.error());
        Json::Writer writer;
        writer.begin_object();
        writer.field("initial", data.initial);
        if(data.library)
            writer.field("library", std::to_string(data.library.value()));
        if(!data.layers.empty()) {
            writer.key("layers");
            writer.begin_array();
            for(const auto& layer : data.layers) {
                writer.begin_object();
                writer.field("name", layer.name);
                writer.field("state", layer.state);
                writer.field("weight", layer.weight);
                writer.field("weight_parameter", layer.weight_parameter);
                writer.key("mask");
                writer.begin_array();
                for(const auto& bone : layer.mask)
                    writer.value(bone);
                writer.end_array();
                writer.end_object();
            }
            writer.end_array();
        }
        writer.key("parameters");
        writer.begin_object();
        for(const auto& [name, value] : data.parameters) {
            writer.key(name);
            write_scalar(writer, value);
        }
        writer.end_object();
        writer.key("states");
        writer.begin_array();
        for(const auto& state : data.states) {
            writer.begin_object();
            writer.field("name", state.name);
            writer.field("parameter", state.parameter);
            if(!state.parameter_y.empty())
                writer.field("parameter_y", state.parameter_y);
            writer.field("smoothing", state.smoothing);
            writer.field("loop", state.loop);
            writer.key("motions");
            writer.begin_array();
            for(const auto& motion : state.motions) {
                writer.begin_object();
                writer.field("threshold", motion.threshold);
                if(!state.parameter_y.empty() || motion.y != 0)
                    writer.field("y", motion.y);
                writer.field("clip", motion.clip);
                writer.end_object();
            }
            writer.end_array();
            if(!state.events.empty()) {
                writer.key("events");
                writer.begin_array();
                for(const auto& event : state.events) {
                    writer.begin_object();
                    writer.field("phase", event.phase);
                    writer.field("name", event.name);
                    writer.key("value");
                    write_scalar(writer, event.value);
                    writer.end_object();
                }
                writer.end_array();
            }
            writer.end_object();
        }
        writer.end_array();
        writer.key("transitions");
        writer.begin_array();
        for(const auto& transition : data.transitions) {
            writer.begin_object();
            writer.field("from", transition.from);
            writer.field("to", transition.to);
            writer.field("duration", transition.duration);
            writer.field("after_finished", transition.after_finished);
            writer.key("conditions");
            writer.begin_array();
            for(const auto& condition : transition.conditions) {
                writer.begin_object();
                writer.field("parameter", condition.parameter);
                writer.field("compare", OPERATORS[std::size_t(condition.compare)]);
                writer.key("value");
                write_scalar(writer, condition.value);
                writer.end_object();
            }
            writer.end_array();
            writer.end_object();
        }
        writer.end_array();
        writer.end_object();
        return std::move(writer).finish();
    }
    Result<AnimationController> AnimationControllerSerializer::load(
        const std::filesystem::path& path) const {
        return Json::load(*this, path);
    }
    Result<void> AnimationControllerSerializer::save(
        const AnimationController& data, const std::filesystem::path& path) const {
        return Json::save(*this, data, path);
    }
}
