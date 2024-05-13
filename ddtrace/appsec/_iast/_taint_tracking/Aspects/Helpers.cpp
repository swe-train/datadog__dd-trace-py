#include "Helpers.h"
#include "Initializer/Initializer.h"
#include <algorithm>
#include <ostream>
#include <regex>

using namespace pybind11::literals;
namespace py = pybind11;

/**
 * @brief This function is used to get the taint ranges for the given text object.
 *
 * @tparam StrType
 * @param text
 * @return TaintRangeRefs
 */
template<class StrType>
StrType
api_common_replace(const py::str& string_method,
                   const StrType& candidate_text,
                   const py::args& args,
                   const py::kwargs& kwargs)
{
    bool ranges_error;
    TaintRangeRefs candidate_text_ranges;
    TaintRangeMapType* tx_map = initializer->get_tainting_map();
    StrType res = py::getattr(candidate_text, string_method)(*args, **kwargs);

    if (not tx_map or tx_map->empty()) {
        return res;
    }
    std::tie(candidate_text_ranges, ranges_error) = get_ranges(candidate_text.ptr(), tx_map);

    if (ranges_error or candidate_text_ranges.empty()) {
        return res;
    }

    set_ranges(res.ptr(), shift_taint_ranges(candidate_text_ranges, 0, -1), tx_map);
    return res;
}

struct EVIDENCE_MARKS
{
    static constexpr const char* BLANK = "";
    static constexpr const char* START_EVIDENCE = ":+-";
    static constexpr const char* END_EVIDENCE = "-+:";
    static constexpr const char* LESS = "<";
    static constexpr const char* GREATER = ">";
};

template<class StrType>
static StrType
get_tag(const py::object& content)
{
    if (content.is_none()) {
        return StrType(EVIDENCE_MARKS::BLANK);
    }

    if (py::isinstance<py::str>(StrType(EVIDENCE_MARKS::LESS))) {
        return StrType(EVIDENCE_MARKS::LESS) + content.cast<py::str>() + StrType(EVIDENCE_MARKS::GREATER);
    }
    return StrType(EVIDENCE_MARKS::LESS) + py::bytes(content.cast<py::str>()) + StrType(EVIDENCE_MARKS::GREATER);
}

// TODO OPTIMIZATION: check if we can use instead a struct object with range_guid_map, new_ranges and default members so
// we dont have to get the keys by string
static py::object
mapper_replace(const TaintRangePtr& taint_range, const optional<const py::dict>& new_ranges)
{
    if (!taint_range or !new_ranges) {
        return py::none{};
    }
    py::object o = py::cast(taint_range);

    if (!new_ranges->contains(o)) {
        return py::none{};
    }
    TaintRange new_range = py::cast<TaintRange>((*new_ranges)[o]);
    return py::int_(new_range.get_hash());
}

py::object
get_default_content(const TaintRangePtr& taint_range)
{
    if (!taint_range->source.name.empty()) {
        return py::str(taint_range->source.name);
    }

    return py::cast<py::none>(Py_None);
}

bool
range_sort(const TaintRangePtr& t1, const TaintRangePtr& t2)
{
    return t1->start < t2->start;
}

template<class StrType>
StrType
_all_as_formatted_evidence(StrType& text, TagMappingMode tag_mapping_mode)
{
    TaintRangeRefs text_ranges = api_get_ranges(text);
    return AsFormattedEvidence<StrType>(text, text_ranges, tag_mapping_mode, nullopt);
}

template<class StrType>
StrType
_int_as_formatted_evidence(StrType& text, TaintRangeRefs text_ranges, TagMappingMode tag_mapping_mode)
{
    return AsFormattedEvidence<StrType>(text, text_ranges, tag_mapping_mode, nullopt);
}

// TODO OPTIMIZATION: Remove py::types once this isn't used in Python
template<class StrType>
StrType
AsFormattedEvidence(StrType& text,
                    TaintRangeRefs& text_ranges,
                    const optional<TagMappingMode>& tag_mapping_mode,
                    const optional<const py::dict>& new_ranges)
{
    if (text_ranges.empty()) {
        return text;
    }
    vector<StrType> res_vector;
    long index = 0;

    sort(text_ranges.begin(), text_ranges.end(), &range_sort);
    for (const auto& taint_range : text_ranges) {
        py::object content;
        if (!tag_mapping_mode) {
            content = get_default_content(taint_range);
        } else
            switch (*tag_mapping_mode) {
                case TagMappingMode::Mapper:
                    content = py::int_(taint_range->get_hash());
                    break;
                case TagMappingMode::Mapper_Replace:
                    content = mapper_replace(taint_range, new_ranges);
                    break;
                default: {
                    // Nothing
                }
            }
        auto tag = get_tag<StrType>(content);

        auto range_end = taint_range->start + taint_range->length;

        res_vector.push_back(text[py::slice(py::int_{ index }, py::int_{ taint_range->start }, nullptr)]);
        res_vector.push_back(StrType(EVIDENCE_MARKS::START_EVIDENCE));
        res_vector.push_back(tag);
        res_vector.push_back(text[py::slice(py::int_{ taint_range->start }, py::int_{ range_end }, nullptr)]);
        res_vector.push_back(tag);
        res_vector.push_back(StrType(EVIDENCE_MARKS::END_EVIDENCE));

        index = range_end;
    }
    res_vector.push_back(text[py::slice(py::int_(index), nullptr, nullptr)]);
    return StrType(EVIDENCE_MARKS::BLANK).attr("join")(res_vector);
}

template<class StrType>
StrType
ApiAsFormattedEvidence(StrType& text,
                       optional<TaintRangeRefs>& text_ranges,
                       const optional<TagMappingMode>& tag_mapping_mode,
                       const optional<const py::dict>& new_ranges)
{
    TaintRangeRefs _ranges;
    if (!text_ranges) {
        _ranges = api_get_ranges(text);
    } else {
        _ranges = text_ranges.value();
    }
    return AsFormattedEvidence<StrType>(text, _ranges, tag_mapping_mode, new_ranges);
}

vector<string>
split_taints(const string& str_to_split)
{
    std::regex rgx(R"((:\+-(<[0-9.a-z\-]+>)?|(<[0-9.a-z\-]+>)?-\+:))");
    std::sregex_token_iterator iter(str_to_split.begin(), str_to_split.end(), rgx, { -1, 0 });
    std::sregex_token_iterator end;
    vector<string> res;

    for (; iter != end; ++iter) {
        res.push_back(*iter);
    }

    return res;
}

py::bytearray
api_convert_escaped_text_to_taint_text_ba(const py::bytearray& taint_escaped_text, TaintRangeRefs ranges_orig)
{

    auto tx_map = initializer->get_tainting_map();

    py::bytes bytes_text = py::bytes() + taint_escaped_text;

    std::tuple result = _convert_escaped_text_to_taint_text<py::bytes>(bytes_text, std::move(ranges_orig));
    PyObject* new_result = new_pyobject_id((py::bytearray() + get<0>(result)).ptr());
    set_ranges(new_result, get<1>(result), tx_map);
    return py::reinterpret_steal<py::bytearray>(new_result);
}

template<class StrType>
StrType
api_convert_escaped_text_to_taint_text(const StrType& taint_escaped_text, TaintRangeRefs ranges_orig)
{
    auto tx_map = initializer->get_tainting_map();

    std::tuple result = _convert_escaped_text_to_taint_text<StrType>(taint_escaped_text, ranges_orig);
    StrType result_text = get<0>(result);
    TaintRangeRefs result_ranges = get<1>(result);
    PyObject* new_result = new_pyobject_id(result_text.ptr());
    set_ranges(new_result, result_ranges, tx_map);
    return py::reinterpret_steal<StrType>(new_result);
}

unsigned long int
getNum(std::string s)
{
    unsigned int n = -1;
    try {
        n = std::stoul(s, nullptr, 10);
        if (errno != 0) {
            PyErr_Print();
        }
    } catch (std::exception& e) {
        // throw std::invalid_argument("Value is too big");
        PyErr_Print();
    }
    return n;
}

template<class StrType>
std::tuple<StrType, TaintRangeRefs>
_convert_escaped_text_to_taint_text(const StrType& taint_escaped_text, TaintRangeRefs ranges_orig)
{
    string result{ u8"" };
    string startswith_element{ ":" };

    string taint_escaped_string = py::cast<string>(taint_escaped_text);
    vector<string> texts_and_marks = split_taints(taint_escaped_string);
    optional<TaintRangeRefs> optional_ranges_orig = ranges_orig;

    vector<tuple<string, int>> context_stack;
    int length, end = 0;
    TaintRangeRefs ranges;

    int latest_end = -1;
    int index = 0;
    int start;
    int prev_context_pos;
    string id_evidence;

    for (string const& element : texts_and_marks) {
        bool is_content = index % 2 == 0;
        if (is_content) {
            result += element;
            length = py::len(StrType(element));
            end += length;
            index++;
            continue;
        }
        if (element.rfind(startswith_element, 0) == 0) {
            id_evidence = element.substr(4, element.length() - 5);
            auto range_by_id = get_range_by_hash(getNum(id_evidence), optional_ranges_orig);
            if (range_by_id == nullptr) {
                result += element;
                length = py::len(StrType(element));
                end += length;
                index++;
                continue;
            }

            if (!context_stack.empty()) {
                auto previous_context = context_stack.back();

                prev_context_pos = get<1>(previous_context);
                if (prev_context_pos > latest_end) {
                    start = prev_context_pos;
                } else {
                    start = latest_end;
                }

                if (start != end) {
                    id_evidence = get<0>(previous_context);
                    const shared_ptr<TaintRange>& original_range =
                      get_range_by_hash(getNum(id_evidence), optional_ranges_orig);
                    ranges.emplace_back(initializer->allocate_taint_range(start, length, original_range->source));
                }
                latest_end = end;
            }
            id_evidence = element.substr(4, element.length() - 5);
            start = end;
            context_stack.push_back({ id_evidence, start });
        } else {
            id_evidence = element.substr(1, element.length() - 5);
            auto range_by_id = get_range_by_hash(getNum(id_evidence), optional_ranges_orig);
            if (range_by_id == nullptr) {
                result += element;
                length = py::len(StrType(element));
                end += length;
                index++;
                continue;
            }

            auto context = context_stack.back();
            context_stack.pop_back();
            prev_context_pos = get<1>(context);
            if (prev_context_pos > latest_end) {
                start = prev_context_pos;
            } else {
                start = latest_end;
            }

            if (start != end) {
                id_evidence = get<0>(context);
                const shared_ptr<TaintRange>& original_range =
                  get_range_by_hash(getNum(id_evidence), optional_ranges_orig);
                ranges.emplace_back(initializer->allocate_taint_range(start, end - start, original_range->source));
            }
            latest_end = end;
        }
        index++;
    }
    return { StrType(result), ranges };
}

/**
 * @brief This function takes the ranges of a string splitted (as in string.split or rsplit or os.path.split) and
 * applies the ranges of the original string to the splitted parts with updated offsets.
 *
 * @param source_str: The original string that was splitted.
 * @param source_ranges: The ranges of the original string.
 * @param split_result: The splitted parts of the original string.
 * @param tx_map: The taint map to apply the ranges.
 * @param include_separator: If the separator should be included in the splitted parts.
 */
template<class StrType>
bool
set_ranges_on_splitted(const StrType& source_str,
                       const TaintRangeRefs& source_ranges,
                       const py::list& split_result,
                       TaintRangeMapType* tx_map,
                       bool include_separator)
{
    bool some_set = false;

    // Some quick shortcuts
    if (source_ranges.empty() or py::len(split_result) == 0 or py::len(source_str) == 0 or not tx_map) {
        return false;
    }

    RANGE_START offset = 0;
    std::string c_source_str = py::cast<std::string>(source_str);
    auto separator_increase = (int)((not include_separator));

    for (const auto& item : split_result) {
        if (not is_text(item.ptr()) or py::len(item) == 0) {
            continue;
        }
        auto c_item = py::cast<std::string>(item);
        TaintRangeRefs item_ranges;

        // Find the item in the source_str.
        const auto start = static_cast<RANGE_START>(c_source_str.find(c_item, offset));
        if (start == -1) {
            continue;
        }
        const auto end = static_cast<RANGE_START>(start + c_item.length());

        // Find what source_ranges match these positions and create a new range with the start and len updated.
        for (const auto& range : source_ranges) {
            auto range_end_abs = range->start + range->length;

            if (range->start < end && range_end_abs > start) {
                // Create a new range with the updated start
                auto new_range_start = std::max(range->start - offset, 0L);
                auto new_range_length = std::min(end - start, (range->length - std::max(0L, offset - range->start)));
                item_ranges.emplace_back(
                  initializer->allocate_taint_range(new_range_start, new_range_length, range->source));
            }
        }
        if (not item_ranges.empty()) {
            set_ranges(item.ptr(), item_ranges, tx_map);
            some_set = true;
        }

        offset += py::len(item) + separator_increase;
    }

    return some_set;
}

template<class StrType>
bool
api_set_ranges_on_splitted(const StrType& source_str,
                           const TaintRangeRefs& source_ranges,
                           const py::list& split_result,
                           bool include_separator)
{
    TaintRangeMapType* tx_map = initializer->get_tainting_map();
    if (not tx_map) {
        throw py::value_error(MSG_ERROR_TAINT_MAP);
    }
    return set_ranges_on_splitted(source_str, source_ranges, split_result, tx_map, include_separator);
}

py::object
parse_params(size_t position,
             const char* keyword_name,
             const py::object& default_value,
             const py::args& args,
             const py::kwargs& kwargs)
{
    if (args.size() >= position + 1) {
        return args[position];
    } else if (kwargs && kwargs.contains(keyword_name)) {
        return kwargs[keyword_name];
    }
    return default_value;
}

void
pyexport_aspect_helpers(py::module& m)
{
    m.def("common_replace", &api_common_replace<py::bytes>, "string_method"_a, "candidate_text"_a);
    m.def("common_replace", &api_common_replace<py::str>, "string_method"_a, "candidate_text"_a);
    m.def("common_replace", &api_common_replace<py::bytearray>, "string_method"_a, "candidate_text"_a);
    m.def("set_ranges_on_splitted",
          &api_set_ranges_on_splitted<py::bytes>,
          "source_str"_a,
          "source_ranges"_a,
          "split_result"_a,
          // cppcheck-suppress assignBoolToPointer
          "include_separator"_a = false);
    m.def("set_ranges_on_splitted",
          &api_set_ranges_on_splitted<py::str>,
          "source_str"_a,
          "source_ranges"_a,
          "split_result"_a,
          // cppcheck-suppress assignBoolToPointer
          "include_separator"_a = false);
    m.def("set_ranges_on_splitted",
          &api_set_ranges_on_splitted<py::bytearray>,
          "source_str"_a,
          "source_ranges"_a,
          "split_result"_a,
          // cppcheck-suppress assignBoolToPointer
          "include_separator"_a = false);
    m.def("_all_as_formatted_evidence",
          &_all_as_formatted_evidence<py::str>,
          "text"_a,
          "tag_mapping_function"_a = nullopt,
          py::return_value_policy::move);
    m.def("_int_as_formatted_evidence",
          &_int_as_formatted_evidence<py::str>,
          "text"_a,
          "text_ranges"_a = nullopt,
          "tag_mapping_function"_a = nullopt,
          py::return_value_policy::move);
    m.def("as_formatted_evidence",
          &ApiAsFormattedEvidence<py::bytes>,
          "text"_a,
          "text_ranges"_a = nullopt,
          "tag_mapping_function"_a = nullopt,
          "new_ranges"_a = nullopt,
          py::return_value_policy::move);
    m.def("as_formatted_evidence",
          &ApiAsFormattedEvidence<py::str>,
          "text"_a,
          "text_ranges"_a = nullopt,
          "tag_mapping_function"_a = nullopt,
          "new_ranges"_a = nullopt,
          py::return_value_policy::move);
    m.def("as_formatted_evidence",
          &ApiAsFormattedEvidence<py::bytearray>,
          "text"_a,
          "text_ranges"_a = nullopt,
          "tag_mapping_function"_a = nullopt,
          "new_ranges"_a = nullopt,
          py::return_value_policy::move);
    m.def("_convert_escaped_text_to_tainted_text",
          &api_convert_escaped_text_to_taint_text<py::bytes>,
          "taint_escaped_text"_a,
          "ranges_orig"_a);
    m.def("_convert_escaped_text_to_tainted_text",
          &api_convert_escaped_text_to_taint_text<py::str>,
          "taint_escaped_text"_a,
          "ranges_orig"_a);
    m.def("_convert_escaped_text_to_tainted_text",
          &api_convert_escaped_text_to_taint_text_ba,
          "taint_escaped_text"_a,
          "ranges_orig"_a);
    m.def("parse_params", &parse_params);
}
