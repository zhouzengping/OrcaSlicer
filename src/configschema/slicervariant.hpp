#pragma once

#include <assert.h>
#include <ctype.h>
#include <functional>
#include <inttypes.h>
#include <math.h>
#include <memory>
#include <optional>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <type_traits>
#include <string_view>
#include <variant>
#include <vector>
#include <unordered_map>

#include "iconfigmetadef.hpp"

namespace ConfigSchema
{
struct Percent
{
    double value = 0.0;

    bool operator==(const Percent& rhs) const;
};

struct FloatOrPercent
{
    double value = 0.0;
    bool is_percent = false;

    bool operator==(const FloatOrPercent& rhs) const;
};

struct EnumValue
{
    int32_t index = -1;

    bool operator==(const EnumValue& rhs) const;
};

struct Point
{
    double m_x = 0.0;
    double m_y = 0.0;

    Point() = default;
    Point(double x, double y)
        : m_x(x), m_y(y)
    {
    }

    double x() const { return m_x; }
    double y() const { return m_y; }

    bool operator==(const Point& rhs) const { return m_x == rhs.m_x && m_y == rhs.m_y; }
    bool operator!=(const Point& rhs) const { return !(*this == rhs); }
};

struct Point3
{
    double m_x = 0.0;
    double m_y = 0.0;
    double m_z = 0.0;

    Point3() = default;
    Point3(double x, double y, double z)
        : m_x(x), m_y(y), m_z(z)
    {
    }

    double x() const { return m_x; }
    double y() const { return m_y; }
    double z() const { return m_z; }

    bool operator==(const Point3& rhs) const { return m_x == rhs.m_x && m_y == rhs.m_y && m_z == rhs.m_z; }
    bool operator!=(const Point3& rhs) const { return !(*this == rhs); }
};

constexpr size_t g_hashBasis = 1469598103934665603ULL;
constexpr size_t g_hashPrime = 1099511628211ULL;

constexpr const char* g_nilString = "nil";
constexpr const char* g_splitCommas = ",";
constexpr const char* g_splitSemicolons = ";";


using ValueType = ConfigMetaType;
class SlicerVariant
{
public:
    using StringVector         = std::vector<std::string>;
    using BoolVector           = std::vector<unsigned char>;
    using IntVector            = std::vector<int>;
    using Int64Vector          = std::vector<int64_t>;
    using FloatVector          = std::vector<double>;
    using PointVector          = std::vector<Point>;
    using Point3Vector         = std::vector<Point3>;
    using PercentVector        = std::vector<Percent>;
    using FloatOrPercentVector = std::vector<FloatOrPercent>;
    using EnumVector           = std::vector<EnumValue>;

    static const std::unordered_map<ValueType, const char*> g_valueTypeNameMap;

public:
    SlicerVariant() = default;
    SlicerVariant(const SlicerVariant& rhs);
    SlicerVariant(SlicerVariant&& rhs) noexcept;
    SlicerVariant& operator=(const SlicerVariant& rhs);
    SlicerVariant& operator=(SlicerVariant&& rhs) noexcept;

    SlicerVariant(bool v);
    SlicerVariant(int v);
    SlicerVariant(int64_t v);
    SlicerVariant(size_t v);
    SlicerVariant(double v);
    SlicerVariant(const char* v);
    SlicerVariant(const std::string& v);
    explicit SlicerVariant(const Point& v);
    explicit SlicerVariant(const Point3& v);
    explicit SlicerVariant(const Percent& v);
    explicit SlicerVariant(const FloatOrPercent& v);
    explicit SlicerVariant(const EnumValue& v);

    explicit SlicerVariant(const BoolVector& v);
    explicit SlicerVariant(const IntVector& v);
    explicit SlicerVariant(const Int64Vector& v);
    explicit SlicerVariant(const StringVector& v);
    explicit SlicerVariant(const FloatVector& v);
    explicit SlicerVariant(const PointVector& v);
    explicit SlicerVariant(const Point3Vector& v);
    explicit SlicerVariant(const FloatOrPercentVector& v);
    explicit SlicerVariant(const EnumVector& v);

    ValueType type() const;
    const char* type_name() const;

    bool is_undef() const;

    bool is_nil() const;

    bool is_scalar() const;
    bool is_vector() const;

    size_t size() const;
    bool empty() const;

    bool can_convert(ValueType t) const;

    bool           to_bool(bool* ok = nullptr) const;
    int            to_int(bool* ok = nullptr) const;
    int64_t        to_int64(bool* ok = nullptr) const;
    double         to_double(bool* ok = nullptr) const;
    std::string    to_string(bool* ok = nullptr) const;
    Percent        to_percent(bool* ok = nullptr) const;
    FloatOrPercent to_float_or_percent(bool* ok = nullptr) const;
    Point          to_point(bool* ok = nullptr) const;
    Point3         to_point3(bool* ok = nullptr) const;
    EnumValue      to_enum(bool* ok = nullptr) const;

    BoolVector           bool_vector(bool* ok = nullptr) const;
    IntVector            int_vector(bool* ok = nullptr) const;
    Int64Vector          int64_vector(bool* ok = nullptr) const;
    FloatVector          float_vector(bool* ok = nullptr) const;
    PercentVector        percent_vector(bool* ok = nullptr) const;
    FloatOrPercentVector float_or_percent_vector(bool* ok = nullptr) const;
    StringVector         string_vector(bool* ok = nullptr) const;
    PointVector          point_vector(bool* ok = nullptr) const;
    Point3Vector         point3_vector(bool* ok = nullptr) const;
    EnumVector           enum_vector(bool* ok = nullptr) const;

    bool           elem_bool(size_t i, bool* ok = nullptr) const;
    int            elem_int(size_t i, bool* ok = nullptr) const;
    int64_t        elem_int64(size_t i, bool* ok = nullptr) const;
    double         elem_double(size_t i, bool* ok = nullptr) const;
    Percent        elem_percent(size_t i, bool* ok = nullptr) const;
    FloatOrPercent elem_float_or_percent(size_t i, bool* ok = nullptr) const;
    std::string    elem_string(size_t i, bool* ok = nullptr) const;
    Point          elem_point(size_t i, bool* ok = nullptr) const;
    Point3         elem_point3(size_t i, bool* ok = nullptr) const;
    EnumValue      elem_enum(size_t i, bool* ok = nullptr) const;
    bool           elem_is_nil(size_t i) const;

    bool append(bool v);
    bool append(int v);
    bool append(int64_t v);
    bool append(size_t v);
    bool append(double v);
    bool append(const char* v);
    bool append(const std::string& v);
    bool append(const Point& v);
    bool append(const Point3& v);
    bool append(const Percent& v);
    bool append(const FloatOrPercent& v);
    bool append(const EnumValue& v);
    bool append_nil();
    bool append_variant(const SlicerVariant& v);

    bool append_parse(ValueType elem, const std::string& s);

    bool appendvec(const BoolVector& v);
    bool appendvec(const IntVector& v);
    bool appendvec(const Int64Vector& v);
    bool appendvec(const FloatVector& v);
    bool appendvec(const StringVector& v);
    bool appendvec(const PointVector& v);
    bool appendvec(const Point3Vector& v);
    bool appendvec(const PercentVector& v);
    bool appendvec(const FloatOrPercentVector& v);
    bool appendvec(const EnumVector& v);

    bool set_elem(size_t i, bool v);
    bool set_elem(size_t i, int v);
    bool set_elem(size_t i, int64_t v);
    bool set_elem(size_t i, size_t v);
    bool set_elem(size_t i, double v);
    bool set_elem(size_t i, const char* v);
    bool set_elem(size_t i, const std::string& v);
    bool set_elem(size_t i, const Point& v);
    bool set_elem(size_t i, const Point3& v);
    bool set_elem(size_t i, const Percent& v);
    bool set_elem(size_t i, const FloatOrPercent& v);
    bool set_elem(size_t i, const EnumValue& v);
    bool set_elem_nil(size_t i);

    void resize(size_t size);
    void apply(const SlicerVariant& v);

    void reset_nil();

    std::string serialize(const std::string& split = g_splitCommas) const;
    std::vector<std::string> serialize_vector() const;
    bool deserialize(const std::string& s, ValueType t);

    static std::optional<SlicerVariant> parse(ValueType t, const std::string& s);
    static std::optional<SlicerVariant> parse_vector(ValueType elem, const std::string& s, const std::string& split = g_splitCommas);

    bool operator==(const SlicerVariant& rhs) const;
    bool operator!=(const SlicerVariant& rhs) const;

    void detach();

    size_t hash() const;

private:
    using NilType = std::monostate;
    using Cell = std::variant<
        NilType,
        bool,
        int64_t,
        double,
        Percent,
        FloatOrPercent,
        std::string,
        Point,
        Point3,
        EnumValue
    >;

    struct Storage
    {
        ValueType         type = ValueType::Undef;
        std::vector<Cell> cells;
    };

    bool append_cell(Cell c, ValueType vector_type);

    template <typename MakeCell>
    bool append_cells(ValueType vector_type, size_t append_Size, MakeCell make);

    bool set_cell_at(size_t i, Cell c, ValueType vector_type);

    const Cell& scalar_ref() const;

    const Cell& elem_ref(size_t i) const;

    const Storage& storage() const;

    Storage& unique_storage();

    static bool is_vector_type(ValueType t);
    static ValueType vector_type_of(ValueType t);
    static ValueType elem_type_of(ValueType t);

    static bool all_cells_is_nil(const Storage& s);
    static const Cell& nil_cell();

    static std::string trim(std::string_view s);
    static bool iequals(std::string_view a, std::string_view b);
    static bool parse_double(std::string_view s, double& out);
    static bool parse_int64(std::string_view s, int64_t& out);

    static bool cell_to_bool(const Cell& c, ValueType t, bool* ok);
    static int64_t cell_to_int(const Cell& c, ValueType t, bool* ok);
    static double cell_to_double(const Cell& c, ValueType t, bool* ok);
    static std::string cell_to_string(const Cell& c, ValueType t, bool* ok);
    static Percent cell_to_percent(const Cell& c, ValueType t, bool* ok);
    static FloatOrPercent cell_to_float_or_percent(const Cell& c, ValueType t, bool* ok);
    static Point cell_to_point(const Cell& c, ValueType t, bool* ok);
    static Point3 cell_to_point3(const Cell& c, ValueType t, bool* ok);
    static EnumValue cell_to_enum(const Cell& c, ValueType t, bool* ok);
    static bool cell_equal(const Cell& a, const Cell& b);

    static size_t hash_mix(size_t h, uint64_t v);
    static size_t hash_text(size_t h, std::string_view s);
    static uint64_t double_bits(double v);
    static size_t hash_cell(const Cell& c);
    static size_t hash_value(const Percent& v);
    static size_t hash_value(const FloatOrPercent& v);
    static size_t hash_value(const EnumValue& v);

    static std::optional<Cell> parse_cell(ValueType t, std::string_view s);

    static bool nil_keyword_applies(ValueType t);
    static bool parse_point2(std::string_view s, Point& out);
    static bool parse_point3(std::string_view s, Point3& out);
    static std::string serialize_cell(const Cell& c, bool as_vector_elem, ValueType elem);

    static const char* skip_spaces(const char* p);
    static bool only_spaces_left(const char* p);
    static std::string format_double(double v);
    static std::string int64_to_string(int64_t v);
    static std::string escape_cstyle(const std::string& in);
    static bool unescape_cstyle(std::string_view in, std::string& out);
    static void split_top_level(std::string_view s, std::string_view sep, std::vector<std::string_view>& out);
    static bool string_needs_quotes(std::string_view s);

private:
    std::shared_ptr<Storage> m_data;
};

inline const std::unordered_map<ValueType, const char*> SlicerVariant::g_valueTypeNameMap =
{
    { ValueType::Undef, "undef" },
    { ValueType::String, "string" },
    { ValueType::Strings, "strings" },
    { ValueType::Int, "int" },
    { ValueType::Ints, "ints" },
    { ValueType::Float, "float" },
    { ValueType::Floats, "floats" },
    { ValueType::Bool, "bool" },
    { ValueType::Bools, "bools" },
    { ValueType::Percent, "percent" },
    { ValueType::Percents, "percents" },
    { ValueType::FloatOrPercent, "float_or_percent" },
    { ValueType::FloatOrPercents, "float_or_percents" },
    { ValueType::Enum, "enum" },
    { ValueType::Enums, "enums" },
    { ValueType::Point, "point" },
    { ValueType::Points, "points" },
    { ValueType::Point3, "point3" },
    { ValueType::Points3, "points3" }
};


inline bool Percent::operator==(const Percent& rhs) const
{
    return value == rhs.value;
}

inline bool FloatOrPercent::operator==(const FloatOrPercent& rhs) const
{
    return value == rhs.value && is_percent == rhs.is_percent;
}

inline bool EnumValue::operator==(const EnumValue& rhs) const
{
    return index == rhs.index;
}

inline bool SlicerVariant::is_vector_type(ValueType t)
{
    return (uint16_t(t) & g_typeVectorFlag) != 0;
}

inline ValueType SlicerVariant::vector_type_of(ValueType t)
{
    return (ValueType)(uint16_t(t) | g_typeVectorFlag);
}

inline ValueType SlicerVariant::elem_type_of(ValueType t)
{
    return (ValueType)(uint16_t(t) & ~g_typeVectorFlag);
}

inline bool SlicerVariant::all_cells_is_nil(const Storage& s)
{
    for (size_t i = 0; i < s.cells.size(); ++i)
        if (!std::holds_alternative<NilType>(s.cells[i]))
            return false;

    return true;
}

inline const SlicerVariant::Cell& SlicerVariant::nil_cell()
{
    static const Cell cell;

    return cell;
}

inline std::string SlicerVariant::trim(std::string_view s)
{
    size_t b = 0;
    size_t e = s.size();

    while (b < e && isspace((unsigned char)s[b]))
        ++b;

    while (e > b && isspace((unsigned char)s[e - 1]))
        --e;

    return std::string(s.substr(b, e - b));
}

inline bool SlicerVariant::iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size())
        return false;

    for (size_t i = 0; i < a.size(); ++i)
    {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return false;
    }

    return true;
}

inline bool SlicerVariant::parse_double(std::string_view s, double& out)
{
    const std::string str(s);
    const char* p = str.c_str();
    char* end = nullptr;

    out = strtod(p, &end);

    if (end == p)
        return false;

    while (*end != '\0' && isspace((unsigned char)*end))
        ++end;

    return *end == '\0';
}

inline bool SlicerVariant::parse_int64(std::string_view s, int64_t& out)
{
    const std::string str(s);
    const char* p = str.c_str();
    char* end = nullptr;

    out = strtoll(p, &end, 10);

    if (end == p)
        return false;

    while (*end != '\0' && isspace((unsigned char)*end))
        ++end;

    return *end == '\0';
}

inline bool SlicerVariant::cell_to_bool(const Cell& c, ValueType t, bool* ok)
{
    if (ok)
        *ok = true;

    switch (t)
    {
    case ValueType::Bool:
        if (const bool* v = std::get_if<bool>(&c))
            return *v;
        break;
    case ValueType::Int:
        if (const int64_t* v = std::get_if<int64_t>(&c))
            return *v != 0;
        break;
    case ValueType::Float:
        if (const double* v = std::get_if<double>(&c))
            return *v != 0.0;
        break;
    case ValueType::String:
        if (const std::string* s = std::get_if<std::string>(&c))
        {
            if (iequals(*s, "true") || iequals(*s, "1") || iequals(*s, "on") || iequals(*s, "yes"))
                return true;

            if (iequals(*s, "false") || iequals(*s, "0") || iequals(*s, "off") || iequals(*s, "no"))
                return false;
        }
        break;
    default:
        break;
    }

    if (ok)
        *ok = false;

    return false;
}

inline int64_t SlicerVariant::cell_to_int(const Cell& c, ValueType t, bool* ok)
{
    if (ok)
        *ok = true;

    switch (t)
    {
    case ValueType::Int:
        if (const int64_t* v = std::get_if<int64_t>(&c))
            return *v;
        break;
    case ValueType::Bool:
        if (const bool* v = std::get_if<bool>(&c))
            return *v ? 1 : 0;
        break;
    case ValueType::Enum:
        if (const EnumValue* v = std::get_if<EnumValue>(&c))
            return v->index;
        break;
    case ValueType::Float:
        if (const double* d = std::get_if<double>(&c))
        {
            constexpr double int64_min = static_cast<double>(INT64_MIN);
            constexpr double int64_end = static_cast<double>(INT64_MAX);

            if (*d == floor(*d) && *d >= int64_min && *d < int64_end)
                return (int64_t)*d;
        }
        break;
    case ValueType::String:
        if (const std::string* s = std::get_if<std::string>(&c))
        {
            int64_t v = 0;

            if (parse_int64(*s, v))
                return v;
        }
        break;
    default:
        break;
    }

    if (ok)
        *ok = false;

    return 0;
}

inline double SlicerVariant::cell_to_double(const Cell& c, ValueType t, bool* ok)
{
    if (ok)
        *ok = true;

    switch (t)
    {
    case ValueType::Float:
        if (const double* v = std::get_if<double>(&c))
            return *v;
        break;
    case ValueType::Percent:
        if (const Percent* v = std::get_if<Percent>(&c))
            return v->value;
        break;
    case ValueType::FloatOrPercent:
        if (const FloatOrPercent* v = std::get_if<FloatOrPercent>(&c))
            return v->value;
        break;
    case ValueType::Int:
        if (const int64_t* v = std::get_if<int64_t>(&c))
            return (double)*v;
        break;
    case ValueType::Bool:
        if (const bool* v = std::get_if<bool>(&c))
            return *v ? 1.0 : 0.0;
        break;
    case ValueType::String:
        if (const std::string* s = std::get_if<std::string>(&c))
        {
            double v = 0.0;

            if (parse_double(*s, v))
                return v;
        }
        break;
    default:
        break;
    }

    if (ok)
        *ok = false;

    return 0.0;
}

inline std::string SlicerVariant::cell_to_string(const Cell& c, ValueType t, bool* ok)
{
    const std::string* s = t == ValueType::String ? std::get_if<std::string>(&c) : nullptr;

    if (ok)
        *ok = s != nullptr;

    return s != nullptr ? *s : std::string();
}

inline Percent SlicerVariant::cell_to_percent(const Cell& c, ValueType t, bool* ok)
{
    if (ok)
        *ok = true;

    switch (t)
    {
    case ValueType::Percent:
        if (const Percent* v = std::get_if<Percent>(&c))
            return *v;
        break;
    case ValueType::FloatOrPercent:
        if (const FloatOrPercent* v = std::get_if<FloatOrPercent>(&c))
            return Percent{ v->value };
        break;
    case ValueType::Float:
        if (const double* v = std::get_if<double>(&c))
            return Percent{ *v };
        break;
    case ValueType::Int:
        if (const int64_t* v = std::get_if<int64_t>(&c))
            return Percent{ (double)*v };
        break;
    default:
        break;
    }

    if (ok)
        *ok = false;

    return Percent();
}

inline FloatOrPercent SlicerVariant::cell_to_float_or_percent(const Cell& c, ValueType t, bool* ok)
{
    if (ok)
        *ok = true;

    switch (t)
    {
    case ValueType::FloatOrPercent:
        if (const FloatOrPercent* v = std::get_if<FloatOrPercent>(&c))
            return *v;
        break;
    case ValueType::Percent:
        if (const Percent* v = std::get_if<Percent>(&c))
            return FloatOrPercent{ v->value, true };
        break;
    case ValueType::Float:
        if (const double* v = std::get_if<double>(&c))
            return FloatOrPercent{ *v, false };
        break;
    case ValueType::Int:
        if (const int64_t* v = std::get_if<int64_t>(&c))
            return FloatOrPercent{ (double)*v, false };
        break;
    default:
        break;
    }

    if (ok)
        *ok = false;

    return FloatOrPercent();
}

inline Point SlicerVariant::cell_to_point(const Cell& c, ValueType t, bool* ok)
{
    const Point* p = t == ValueType::Point ? std::get_if<Point>(&c) : nullptr;

    if (ok)
        *ok = p != nullptr;

    return p != nullptr ? *p : Point(0.0, 0.0);
}

inline Point3 SlicerVariant::cell_to_point3(const Cell& c, ValueType t, bool* ok)
{
    const Point3* p = t == ValueType::Point3 ? std::get_if<Point3>(&c) : nullptr;

    if (ok)
        *ok = p != nullptr;

    return p != nullptr ? *p : Point3(0.0, 0.0, 0.0);
}

inline EnumValue SlicerVariant::cell_to_enum(const Cell& c, ValueType t, bool* ok)
{
    if (ok)
        *ok = true;

    switch (t)
    {
    case ValueType::Enum:
        if (const EnumValue* v = std::get_if<EnumValue>(&c))
            return *v;
        break;
    case ValueType::Int:
        if (const int64_t* i = std::get_if<int64_t>(&c))
        {
            if (*i >= INT32_MIN && *i <= INT32_MAX)
                return EnumValue{ (int32_t)*i };
        }
        break;
    default:
        break;
    }

    if (ok)
        *ok = false;

    return EnumValue();
}

inline bool SlicerVariant::cell_equal(const Cell& a, const Cell& b)
{
    if (a.index() != b.index())
        return false;

    if (const bool* x = std::get_if<bool>(&a))
        return *x == std::get<bool>(b);

    if (const int64_t* x = std::get_if<int64_t>(&a))
        return *x == std::get<int64_t>(b);

    if (const double* x = std::get_if<double>(&a))
        return *x == std::get<double>(b);

    if (const Percent* x = std::get_if<Percent>(&a))
        return *x == std::get<Percent>(b);

    if (const FloatOrPercent* x = std::get_if<FloatOrPercent>(&a))
        return *x == std::get<FloatOrPercent>(b);

    if (const std::string* x = std::get_if<std::string>(&a))
        return *x == std::get<std::string>(b);

    if (const Point* x = std::get_if<Point>(&a))
    {
        const Point& y = std::get<Point>(b);

        return x->x() == y.x() && x->y() == y.y();
    }

    if (const Point3* x = std::get_if<Point3>(&a))
    {
        const Point3& y = std::get<Point3>(b);

        return x->x() == y.x() && x->y() == y.y() && x->z() == y.z();
    }

    if (const EnumValue* x = std::get_if<EnumValue>(&a))
        return *x == std::get<EnumValue>(b);

    return std::holds_alternative<NilType>(a);
}

inline size_t SlicerVariant::hash_mix(size_t h, uint64_t v)
{
    h ^= v;
    return h * g_hashPrime;
}

inline size_t SlicerVariant::hash_text(size_t h, std::string_view s)
{
    for (size_t i = 0; i < s.size(); ++i)
        h = hash_mix(h, (unsigned char)s[i]);

    return h;
}

inline uint64_t SlicerVariant::double_bits(double v)
{
    if (v == 0.0)
        v = 0.0;

    uint64_t bits = 0;
    memcpy(&bits, &v, sizeof(bits));

    return bits;
}

inline size_t SlicerVariant::hash_cell(const Cell& c)
{
    return std::visit([&c](const auto& v) -> size_t {
        using T = std::decay_t<decltype(v)>;

        size_t h = hash_mix(g_hashBasis, c.index());

        if constexpr (std::is_same_v<T, NilType>)
            return h;
        else if constexpr (std::is_same_v<T, bool>)
            return hash_mix(h, v ? 1u : 0u);
        else if constexpr (std::is_same_v<T, int64_t>)
            return hash_mix(h, (uint64_t)v);
        else if constexpr (std::is_same_v<T, double>)
            return hash_mix(h, double_bits(v));
        else if constexpr (std::is_same_v<T, Percent>)
            return hash_mix(h, double_bits(v.value));
        else if constexpr (std::is_same_v<T, FloatOrPercent>)
        {
            h = hash_mix(h, double_bits(v.value));
            return hash_mix(h, v.is_percent ? 1u : 0u);
        }
        else if constexpr (std::is_same_v<T, std::string>)
            return hash_text(h, v);
        else if constexpr (std::is_same_v<T, Point>)
        {
            h = hash_mix(h, double_bits(v.x()));
            return hash_mix(h, double_bits(v.y()));
        }
        else if constexpr (std::is_same_v<T, Point3>)
        {
            h = hash_mix(h, double_bits(v.x()));
            h = hash_mix(h, double_bits(v.y()));
            return hash_mix(h, double_bits(v.z()));
        }
        else if constexpr (std::is_same_v<T, EnumValue>)
            return hash_mix(h, (uint32_t)v.index);
        else
            static_assert(!sizeof(T*), "SlicerVariant::hash_cell: unhandled Cell alternative");
    }, c);
}

inline size_t SlicerVariant::hash_value(const Percent& v)
{
    return hash_mix(g_hashBasis, double_bits(v.value));
}

inline size_t SlicerVariant::hash_value(const FloatOrPercent& v)
{
    const size_t h = hash_mix(g_hashBasis, double_bits(v.value));

    return hash_mix(h, v.is_percent ? 1u : 0u);
}

inline size_t SlicerVariant::hash_value(const EnumValue& v)
{
    return hash_mix(g_hashBasis, (uint32_t)v.index);
}


inline SlicerVariant::SlicerVariant(const SlicerVariant& rhs)
    : m_data(rhs.m_data)
{
}

inline SlicerVariant::SlicerVariant(SlicerVariant&& rhs) noexcept
    : m_data(std::move(rhs.m_data))
{
}

inline SlicerVariant& SlicerVariant::operator=(const SlicerVariant& rhs)
{
    if (this != &rhs)
        m_data = rhs.m_data;

    return *this;
}

inline SlicerVariant& SlicerVariant::operator=(SlicerVariant&& rhs) noexcept
{
    if (this != &rhs)
        m_data = std::move(rhs.m_data);

    return *this;
}

inline SlicerVariant::SlicerVariant(bool v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Bool;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(int v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Int;
    s.cells.push_back(Cell(int64_t(v)));
}

inline SlicerVariant::SlicerVariant(int64_t v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Int;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(size_t v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Int;
    s.cells.push_back(Cell(int64_t(v)));
}

inline SlicerVariant::SlicerVariant(double v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Float;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(const char* v)
{
    Storage& s = unique_storage();

    s.type = ValueType::String;
    s.cells.push_back(Cell(std::string(v)));
}

inline SlicerVariant::SlicerVariant(const std::string& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::String;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(const Point& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Point;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(const Point3& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Point3;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(const Percent& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Percent;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(const FloatOrPercent& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::FloatOrPercent;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(const EnumValue& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Enum;
    s.cells.push_back(Cell(v));
}

inline SlicerVariant::SlicerVariant(const BoolVector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Bools;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(v[i] != 0));
}

inline SlicerVariant::SlicerVariant(const IntVector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Ints;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(int64_t(v[i])));
}

inline SlicerVariant::SlicerVariant(const Int64Vector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Ints;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(v[i]));
}

inline SlicerVariant::SlicerVariant(const StringVector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Strings;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(v[i]));
}

inline SlicerVariant::SlicerVariant(const FloatVector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Floats;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(v[i]));
}

inline SlicerVariant::SlicerVariant(const PointVector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Points;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(v[i]));
}

inline SlicerVariant::SlicerVariant(const Point3Vector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Points3;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(v[i]));
}

inline SlicerVariant::SlicerVariant(const FloatOrPercentVector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::FloatOrPercents;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(v[i]));
}

inline SlicerVariant::SlicerVariant(const EnumVector& v)
{
    Storage& s = unique_storage();

    s.type = ValueType::Enums;
    s.cells.reserve(v.size());

    for (size_t i = 0; i < v.size(); ++i)
        s.cells.push_back(Cell(v[i]));
}

inline ValueType SlicerVariant::type() const
{
    return storage().type;
}

inline const char* SlicerVariant::type_name() const
{
    const auto it = g_valueTypeNameMap.find(storage().type);
    return it != g_valueTypeNameMap.end() ? it->second : "?";
}

inline bool SlicerVariant::is_undef() const
{
    return storage().type == ValueType::Undef;
}

inline bool SlicerVariant::is_nil() const
{
    const Storage& s = storage();

    if (s.type == ValueType::Undef)
        return true;

    for (size_t i = 0; i < s.cells.size(); ++i)
        if (!std::holds_alternative<NilType>(s.cells[i]))
            return false;

    return true;
}

inline bool SlicerVariant::is_scalar() const
{
    return !is_vector_type(storage().type);
}

inline bool SlicerVariant::is_vector() const
{
    return is_vector_type(storage().type);
}

inline size_t SlicerVariant::size() const
{
    return storage().cells.size();
}

inline bool SlicerVariant::empty() const
{
    return size() == 0;
}

inline bool SlicerVariant::can_convert(ValueType t) const
{
    bool ok = false;

    switch (t)
    {
    case ValueType::Bool:
        to_bool(&ok);
        break;
    case ValueType::Int:
        to_int(&ok);
        break;
    case ValueType::Float:
        to_double(&ok);
        break;
    case ValueType::Percent:
        to_percent(&ok);
        break;
    case ValueType::FloatOrPercent:
        to_float_or_percent(&ok);
        break;
    case ValueType::String:
        to_string(&ok);
        break;
    case ValueType::Point:
        to_point(&ok);
        break;
    case ValueType::Point3:
        to_point3(&ok);
        break;
    case ValueType::Enum:
        to_enum(&ok);
        break;
    case ValueType::Undef:
        return is_undef();
    default:
        break;
    }

    return ok;
}

inline bool SlicerVariant::to_bool(bool* ok) const
{
    return cell_to_bool(scalar_ref(), elem_type_of(storage().type), ok);
}

inline int SlicerVariant::to_int(bool* ok) const
{
    const int64_t v = to_int64(ok);

    if (v < INT32_MIN || v > INT32_MAX)
    {
        if (ok)
            *ok = false;

        return 0;
    }

    return (int)v;
}

inline int64_t SlicerVariant::to_int64(bool* ok) const
{
    return cell_to_int(scalar_ref(), elem_type_of(storage().type), ok);
}

inline double SlicerVariant::to_double(bool* ok) const
{
    return cell_to_double(scalar_ref(), elem_type_of(storage().type), ok);
}

inline std::string SlicerVariant::to_string(bool* ok) const
{
    return cell_to_string(scalar_ref(), elem_type_of(storage().type), ok);
}

inline Percent SlicerVariant::to_percent(bool* ok) const
{
    return cell_to_percent(scalar_ref(), elem_type_of(storage().type), ok);
}

inline FloatOrPercent SlicerVariant::to_float_or_percent(bool* ok) const
{
    return cell_to_float_or_percent(scalar_ref(), elem_type_of(storage().type), ok);
}

inline Point SlicerVariant::to_point(bool* ok) const
{
    return cell_to_point(scalar_ref(), elem_type_of(storage().type), ok);
}

inline Point3 SlicerVariant::to_point3(bool* ok) const
{
    return cell_to_point3(scalar_ref(), elem_type_of(storage().type), ok);
}

inline EnumValue SlicerVariant::to_enum(bool* ok) const
{
    return cell_to_enum(scalar_ref(), elem_type_of(storage().type), ok);
}

inline SlicerVariant::BoolVector SlicerVariant::bool_vector(bool* ok) const
{
    BoolVector out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const bool v = cell_to_bool(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return BoolVector();

        out.push_back((unsigned char)v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<int> SlicerVariant::int_vector(bool* ok) const
{
    std::vector<int> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const int64_t v = cell_to_int(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem || v < INT32_MIN || v > INT32_MAX)
            return std::vector<int>();

        out.push_back((int)v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<int64_t> SlicerVariant::int64_vector(bool* ok) const
{
    std::vector<int64_t> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const int64_t v = cell_to_int(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return std::vector<int64_t>();

        out.push_back(v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<double> SlicerVariant::float_vector(bool* ok) const
{
    std::vector<double> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const double v = cell_to_double(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return std::vector<double>();

        out.push_back(v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<Percent> SlicerVariant::percent_vector(bool* ok) const
{
    std::vector<Percent> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const Percent v = cell_to_percent(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return std::vector<Percent>();

        out.push_back(v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<FloatOrPercent> SlicerVariant::float_or_percent_vector(bool* ok) const
{
    std::vector<FloatOrPercent> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const FloatOrPercent v = cell_to_float_or_percent(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return std::vector<FloatOrPercent>();

        out.push_back(v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<std::string> SlicerVariant::string_vector(bool* ok) const
{
    std::vector<std::string> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const std::string v = cell_to_string(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return std::vector<std::string>();

        out.push_back(v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<Point> SlicerVariant::point_vector(bool* ok) const
{
    std::vector<Point> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const Point v = cell_to_point(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return std::vector<Point>();

        out.push_back(v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<Point3> SlicerVariant::point3_vector(bool* ok) const
{
    std::vector<Point3> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const Point3 v = cell_to_point3(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return std::vector<Point3>();

        out.push_back(v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline std::vector<EnumValue> SlicerVariant::enum_vector(bool* ok) const
{
    std::vector<EnumValue> out;

    if (ok)
        *ok = false;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        bool ok_elem = false;
        const EnumValue v = cell_to_enum(s.cells[i], elem_type_of(s.type), &ok_elem);

        if (!ok_elem)
            return std::vector<EnumValue>();

        out.push_back(v);
    }

    if (ok)
        *ok = true;

    return out;
}

inline bool SlicerVariant::elem_bool(size_t i, bool* ok) const
{
    return cell_to_bool(elem_ref(i), elem_type_of(storage().type), ok);
}

inline int SlicerVariant::elem_int(size_t i, bool* ok) const
{
    return (int)cell_to_int(elem_ref(i), elem_type_of(storage().type), ok);
}

inline int64_t SlicerVariant::elem_int64(size_t i, bool* ok) const
{
    return cell_to_int(elem_ref(i), elem_type_of(storage().type), ok);
}

inline double SlicerVariant::elem_double(size_t i, bool* ok) const
{
    return cell_to_double(elem_ref(i), elem_type_of(storage().type), ok);
}

inline Percent SlicerVariant::elem_percent(size_t i, bool* ok) const
{
    return cell_to_percent(elem_ref(i), elem_type_of(storage().type), ok);
}

inline FloatOrPercent SlicerVariant::elem_float_or_percent(size_t i, bool* ok) const
{
    return cell_to_float_or_percent(elem_ref(i), elem_type_of(storage().type), ok);
}

inline std::string SlicerVariant::elem_string(size_t i, bool* ok) const
{
    return cell_to_string(elem_ref(i), elem_type_of(storage().type), ok);
}

inline Point SlicerVariant::elem_point(size_t i, bool* ok) const
{
    return cell_to_point(elem_ref(i), elem_type_of(storage().type), ok);
}

inline Point3 SlicerVariant::elem_point3(size_t i, bool* ok) const
{
    return cell_to_point3(elem_ref(i), elem_type_of(storage().type), ok);
}

inline EnumValue SlicerVariant::elem_enum(size_t i, bool* ok) const
{
    return cell_to_enum(elem_ref(i), elem_type_of(storage().type), ok);
}

inline bool SlicerVariant::elem_is_nil(size_t i) const
{
    return std::holds_alternative<NilType>(elem_ref(i));
}

inline bool SlicerVariant::append(bool v)
{
    return append_cell(Cell(v), ValueType::Bools);
}

inline bool SlicerVariant::append(int v)
{
    return append_cell(Cell(int64_t(v)), ValueType::Ints);
}

inline bool SlicerVariant::append(int64_t v)
{
    return append_cell(Cell(v), ValueType::Ints);
}

inline bool SlicerVariant::append(size_t v)
{
    return append_cell(Cell(int64_t(v)), ValueType::Ints);
}

inline bool SlicerVariant::append(double v)
{
    return append_cell(Cell(v), ValueType::Floats);
}

inline bool SlicerVariant::append(const char* v)
{
    return append_cell(Cell(std::string(v)), ValueType::Strings);
}

inline bool SlicerVariant::append(const std::string& v)
{
    return append_cell(Cell(v), ValueType::Strings);
}

inline bool SlicerVariant::append(const Point& v)
{
    return append_cell(Cell(v), ValueType::Points);
}

inline bool SlicerVariant::append(const Point3& v)
{
    return append_cell(Cell(v), ValueType::Points3);
}

inline bool SlicerVariant::append(const Percent& v)
{
    return append_cell(Cell(v), ValueType::Percents);
}

inline bool SlicerVariant::append(const FloatOrPercent& v)
{
    return append_cell(Cell(v), ValueType::FloatOrPercents);
}

inline bool SlicerVariant::append(const EnumValue& v)
{
    return append_cell(Cell(v), ValueType::Enums);
}

inline bool SlicerVariant::append_nil()
{
    assert(m_data && is_vector_type(m_data->type));
    if (!m_data || !is_vector_type(m_data->type))
        return false;

    return append_cell(Cell(), m_data->type);
}

inline bool SlicerVariant::append_variant(const SlicerVariant& v)
{
    assert(m_data && v.type() == type());
    if (!m_data || v.type() != type())
        return false;

    const Storage& src = v.storage();

    if (src.cells.empty())
        return true;

    Storage& dst = unique_storage();

    const size_t n = src.cells.size();

    dst.cells.reserve(dst.cells.size() + n);

    for (size_t i = 0; i < n; ++i)
        dst.cells.push_back(src.cells[i]);

    return true;
}

template <typename MakeCell>
inline bool SlicerVariant::append_cells(ValueType vector_type, size_t append_Size, MakeCell make)
{
    assert(m_data && m_data->type == vector_type);
    if (!m_data || m_data->type != vector_type)
        return false;

    if (append_Size == 0)
        return true;

    Storage& dst = unique_storage();

    dst.cells.reserve(dst.cells.size() + append_Size);

    for (size_t i = 0; i < append_Size; ++i)
        dst.cells.push_back(make(i));

    return true;
}

inline bool SlicerVariant::appendvec(const BoolVector& v)
{
    return append_cells(ValueType::Bools, v.size(), [&v](size_t i) { return Cell(v[i] != 0); });
}

inline bool SlicerVariant::appendvec(const IntVector& v)
{
    return append_cells(ValueType::Ints, v.size(), [&v](size_t i) { return Cell(int64_t(v[i])); });
}

inline bool SlicerVariant::appendvec(const Int64Vector& v)
{
    return append_cells(ValueType::Ints, v.size(), [&v](size_t i) { return Cell(v[i]); });
}

inline bool SlicerVariant::appendvec(const FloatVector& v)
{
    return append_cells(ValueType::Floats, v.size(), [&v](size_t i) { return Cell(v[i]); });
}

inline bool SlicerVariant::appendvec(const StringVector& v)
{
    return append_cells(ValueType::Strings, v.size(), [&v](size_t i) { return Cell(v[i]); });
}

inline bool SlicerVariant::appendvec(const PointVector& v)
{
    return append_cells(ValueType::Points, v.size(), [&v](size_t i) { return Cell(v[i]); });
}

inline bool SlicerVariant::appendvec(const Point3Vector& v)
{
    return append_cells(ValueType::Points3, v.size(), [&v](size_t i) { return Cell(v[i]); });
}

inline bool SlicerVariant::appendvec(const PercentVector& v)
{
    return append_cells(ValueType::Percents, v.size(), [&v](size_t i) { return Cell(v[i]); });
}

inline bool SlicerVariant::appendvec(const FloatOrPercentVector& v)
{
    return append_cells(ValueType::FloatOrPercents, v.size(), [&v](size_t i) { return Cell(v[i]); });
}

inline bool SlicerVariant::appendvec(const EnumVector& v)
{
    return append_cells(ValueType::Enums, v.size(), [&v](size_t i) { return Cell(v[i]); });
}

inline bool SlicerVariant::append_cell(Cell c, ValueType vector_type)
{
    assert(m_data && vector_type == m_data->type);
    if (!m_data || vector_type != m_data->type)
        return false;

    Storage& s = unique_storage();

    s.cells.push_back(std::move(c));

    return true;
}

inline bool SlicerVariant::set_cell_at(size_t i, Cell c, ValueType vector_type)
{
    assert(m_data && vector_type == m_data->type);
    if (!m_data || vector_type != m_data->type)
        return false;

    Storage& s = unique_storage();

    if (i >= s.cells.size())
        s.cells.resize(i + 1);

    s.cells[i] = std::move(c);

    return true;
}

inline bool SlicerVariant::set_elem(size_t i, bool v)
{
    return set_cell_at(i, Cell(v), ValueType::Bools);
}

inline bool SlicerVariant::set_elem(size_t i, int v)
{
    return set_cell_at(i, Cell(int64_t(v)), ValueType::Ints);
}

inline bool SlicerVariant::set_elem(size_t i, int64_t v)
{
    return set_cell_at(i, Cell(v), ValueType::Ints);
}

inline bool SlicerVariant::set_elem(size_t i, size_t v)
{
    return set_cell_at(i, Cell(int64_t(v)), ValueType::Ints);
}

inline bool SlicerVariant::set_elem(size_t i, double v)
{
    return set_cell_at(i, Cell(v), ValueType::Floats);
}

inline bool SlicerVariant::set_elem(size_t i, const char* v)
{
    return set_cell_at(i, Cell(std::string(v)), ValueType::Strings);
}

inline bool SlicerVariant::set_elem(size_t i, const std::string& v)
{
    return set_cell_at(i, Cell(v), ValueType::Strings);
}

inline bool SlicerVariant::set_elem(size_t i, const Point& v)
{
    return set_cell_at(i, Cell(v), ValueType::Points);
}

inline bool SlicerVariant::set_elem(size_t i, const Point3& v)
{
    return set_cell_at(i, Cell(v), ValueType::Points3);
}

inline bool SlicerVariant::set_elem(size_t i, const Percent& v)
{
    return set_cell_at(i, Cell(v), ValueType::Percents);
}

inline bool SlicerVariant::set_elem(size_t i, const FloatOrPercent& v)
{
    return set_cell_at(i, Cell(v), ValueType::FloatOrPercents);
}

inline bool SlicerVariant::set_elem(size_t i, const EnumValue& v)
{
    return set_cell_at(i, Cell(v), ValueType::Enums);
}

inline bool SlicerVariant::set_elem_nil(size_t i)
{
    assert(m_data && is_vector_type(m_data->type));
    if (!m_data || !is_vector_type(m_data->type))
        return false;

    Storage& s = unique_storage();

    if (i >= s.cells.size())
        s.cells.resize(i + 1);

    s.cells[i] = Cell();

    return true;
}

inline void SlicerVariant::resize(size_t size)
{
    const size_t n = storage().cells.size();

    if (size == n)
        return;

    Storage& s = unique_storage();

    if (size < n)
    {
        s.cells.erase(s.cells.begin() + size, s.cells.end());
    }
    else if (!s.cells.empty())
    {
        const Cell fill = s.cells.front();
        s.cells.resize(size, fill);
    }
}

inline void SlicerVariant::apply(const SlicerVariant& v)
{
    if (this == &v)
        return;

    if (is_undef())
    {
        if (!v.is_undef())
            *this = v;
        return;
    }

    if (v.is_undef())
        return;

    assert(v.type() == type());
    if (v.type() != type())
        return;

    const Storage& src = v.storage();
    const size_t   n   = src.cells.size();

    Storage& dst = unique_storage();

    for (size_t i = 0; i < n; ++i)
    {
        const Cell& c = src.cells[i];

        if (i < dst.cells.size())
        {
            if (!std::holds_alternative<NilType>(c))
                dst.cells[i] = c;
        }
        else if (!std::holds_alternative<NilType>(c))
        {
            dst.cells.push_back(c);
        }
        else
        {
            dst.cells.push_back(Cell());
        }
    }

    if (dst.cells.size() > n)
        dst.cells.erase(dst.cells.begin() + n, dst.cells.end());
}

inline void SlicerVariant::reset_nil()
{
    const ValueType t = type();

    if (t == ValueType::Undef)
        return;

    const size_t n = storage().cells.size();
    Storage& s = unique_storage();

    s.cells.assign(n, Cell());
}

inline void SlicerVariant::detach()
{
    unique_storage();
}

inline bool SlicerVariant::operator==(const SlicerVariant& rhs) const
{
    if (m_data == rhs.m_data)
        return true;

    const Storage& a = storage();
    const Storage& b = rhs.storage();

    if (a.type != b.type)
        return false;

    if (a.cells.size() != b.cells.size())
        return false;

    for (size_t i = 0; i < a.cells.size(); ++i)
    {
        if (!cell_equal(a.cells[i], b.cells[i]))
            return false;
    }

    return true;
}

inline bool SlicerVariant::operator!=(const SlicerVariant& rhs) const
{
    return !(*this == rhs);
}

inline size_t SlicerVariant::hash() const
{
    const Storage& s = storage();

    size_t h = hash_mix(g_hashBasis, uint16_t(s.type));

    for (size_t i = 0; i < s.cells.size(); ++i)
        h = hash_mix(h, hash_cell(s.cells[i]));

    return h;
}

inline const SlicerVariant::Cell& SlicerVariant::scalar_ref() const
{
    const Storage& s = storage();

    return !is_vector_type(s.type) && !s.cells.empty() ? s.cells[0] : nil_cell();
}

inline const SlicerVariant::Cell& SlicerVariant::elem_ref(size_t i) const
{
    assert(i < storage().cells.size());
    const Storage& s = storage();

    if (s.cells.empty())
        return nil_cell();

    return s.cells[i < s.cells.size() ? i : 0];
}

inline const SlicerVariant::Storage& SlicerVariant::storage() const
{
    static const Storage s_nil;

    return m_data ? *m_data : s_nil;
}

inline SlicerVariant::Storage& SlicerVariant::unique_storage()
{
    if (!m_data)
        m_data = std::make_shared<Storage>();
    else if (m_data.use_count() > 1)
        m_data = std::make_shared<Storage>(*m_data);

    return *m_data;
}

inline const char* SlicerVariant::skip_spaces(const char* p)
{
    while (*p != '\0' && isspace((unsigned char)*p))
        ++p;

    return p;
}

inline bool SlicerVariant::only_spaces_left(const char* p)
{
    return *skip_spaces(p) == '\0';
}

inline std::string SlicerVariant::format_double(double v)
{
    char buf[64];

    snprintf(buf, sizeof(buf), "%.15g", v);

    double back = strtod(buf, nullptr);
    if (back != v)
        snprintf(buf, sizeof(buf), "%.17g", v);

    return std::string(buf);
}

inline std::string SlicerVariant::int64_to_string(int64_t v)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%" PRId64, v);

    return std::string(buf);
}

inline std::string SlicerVariant::escape_cstyle(const std::string& in)
{
    std::string out;
    out.reserve(in.size() + 8);

    for (size_t i = 0; i < in.size(); ++i)
    {
        char c = in[i];

        switch (c)
        {
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        default:
            out += c;
            break;
        }
    }

    return out;
}

inline bool SlicerVariant::unescape_cstyle(std::string_view in, std::string& out)
{
    out.clear();
    out.reserve(in.size());

    for (size_t i = 0; i < in.size(); ++i)
    {
        if (in[i] != '\\')
        {
            out += in[i];
            continue;
        }

        ++i;
        if (i >= in.size())
            return false;

        switch (in[i])
        {
        case 'n':
            out += '\n';
            break;
        case 'r':
            out += '\r';
            break;
        case 't':
            out += '\t';
            break;
        case '\\':
            out += '\\';
            break;
        case '"':
            out += '"';
            break;
        default:
            return false;
        }
    }

    return true;
}

inline void SlicerVariant::split_top_level(std::string_view s, std::string_view sep, std::vector<std::string_view>& out)
{
    size_t start = 0;
    bool in_quote = false;
    bool escaped = false;

    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];

        if (escaped)
        {
            escaped = false;
            continue;
        }

        if (in_quote)
        {
            if (c == '\\')
                escaped = true;
            else if (c == '"')
                in_quote = false;

            continue;
        }

        if (c == '"')
        {
            in_quote = true;
            continue;
        }

        if (!sep.empty() && s.compare(i, sep.size(), sep) == 0)
        {
            out.push_back(s.substr(start, i - start));
            i += sep.size() - 1;
            start = i + 1;
        }
    }

    out.push_back(s.substr(start));
}

inline bool SlicerVariant::string_needs_quotes(std::string_view s)
{
    if (s.empty())
        return true;

    for (size_t i = 0; i < s.size(); ++i)
    {
        const char c = s[i];

        if (isspace((unsigned char)c) || c == '"' || c == '\\' || c == ';' || c == ',')
            return true;
    }

    return false;
}

inline bool SlicerVariant::parse_point2(std::string_view s, Point& out)
{
    const std::string str = trim(s);
    const char* p = str.c_str();
    char* end = nullptr;

    double x = strtod(p, &end);
    if (end == p)
        return false;

    p = skip_spaces(end);
    if (*p != ',' && *p != 'x' && *p != 'X')
        return false;

    double y = strtod(p + 1, &end);
    if (end == p + 1)
        return false;

    if (!only_spaces_left(end))
        return false;

    out = Point(x, y);
    return true;
}

inline bool SlicerVariant::parse_point3(std::string_view s, Point3& out)
{
    const std::string str = trim(s);
    const char* p = str.c_str();
    char* end = nullptr;

    double x = strtod(p, &end);
    if (end == p)
        return false;

    p = skip_spaces(end);
    if (*p != ',' && *p != 'x' && *p != 'X')
        return false;

    double y = strtod(p + 1, &end);
    if (end == p + 1)
        return false;

    p = skip_spaces(end);
    if (*p != ',' && *p != 'x' && *p != 'X')
        return false;

    double z = strtod(p + 1, &end);
    if (end == p + 1)
        return false;

    if (!only_spaces_left(end))
        return false;

    out = Point3(x, y, z);
    return true;
}

inline std::optional<SlicerVariant::Cell> SlicerVariant::parse_cell(ValueType t, std::string_view s)
{
    switch (t)
    {
    case ValueType::Undef:
        break;

    case ValueType::Bool:
    {
        const std::string v = trim(s);

        if (iequals(v, "true") || iequals(v, "1") || iequals(v, "on") || iequals(v, "yes"))
            return Cell(true);

        if (iequals(v, "false") || iequals(v, "0") || iequals(v, "off") || iequals(v, "no"))
            return Cell(false);

        break;
    }

    case ValueType::Int:
    {
        int64_t i = 0;

        if (parse_int64(s, i))
            return Cell(i);

        break;
    }

    case ValueType::Float:
    {
        double d = 0.0;

        if (parse_double(s, d))
            return Cell(d);

        break;
    }

    case ValueType::Percent:
    case ValueType::FloatOrPercent:
    {
        std::string v = trim(s);
        bool is_percent = false;

        if (!v.empty() && v.back() == '%')
        {
            is_percent = true;
            v.pop_back();
        }

        double d = 0.0;

        if (parse_double(v, d))
        {
            if (t == ValueType::Percent)
                return Cell(Percent{ d });

            return Cell(FloatOrPercent{ d, is_percent });
        }

        break;
    }

    case ValueType::String:
    {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
        {
            std::string out;

            if (unescape_cstyle(s.substr(1, s.size() - 2), out))
                return Cell(out);

            return Cell(std::string(s));
        }

        std::string out;

        if (unescape_cstyle(s, out))
            return Cell(out);

        return Cell(std::string(s));
    }

    case ValueType::Point:
    {
        Point p;

        if (parse_point2(s, p))
            return Cell(p);

        break;
    }

    case ValueType::Point3:
    {
        Point3 p;

        if (parse_point3(s, p))
            return Cell(p);

        break;
    }

    case ValueType::Enum:
    {
        int64_t i = 0;

        if (parse_int64(s, i) && i >= INT32_MIN && i <= INT32_MAX)
            return Cell(EnumValue{ (int32_t)i });

        break;
    }

    default:
        break;
    }

    return std::nullopt;
}

inline std::optional<SlicerVariant> SlicerVariant::parse(ValueType t, const std::string& s)
{
    const std::optional<Cell> c = parse_cell(t, s);

    if (!c || std::holds_alternative<NilType>(*c))
        return std::nullopt;

    SlicerVariant v;
    Storage& st = v.unique_storage();

    st.type = t;
    st.cells.push_back(*c);

    return v;
}

inline bool SlicerVariant::nil_keyword_applies(ValueType t)
{
    switch (t)
    {
    case ValueType::Bool:
    case ValueType::Int:
    case ValueType::Float:
    case ValueType::Percent:
    case ValueType::FloatOrPercent:
    case ValueType::Enum:
        return true;
    default:
        return false;
    }
}

inline bool SlicerVariant::append_parse(ValueType elem, const std::string& s)
{
    if (!m_data || !is_vector_type(m_data->type) || elem_type_of(m_data->type) != elem)
        return false;

    const std::string piece = trim(s);

    if (piece.empty() || (nil_keyword_applies(elem) && piece == g_nilString))
        return append_cell(Cell(), m_data->type);

    const std::optional<Cell> c = parse_cell(elem, s);

    if (!c)
        return false;

    return append_cell(*c, m_data->type);
}

inline std::optional<SlicerVariant> SlicerVariant::parse_vector(ValueType elem, const std::string& s, const std::string& split)
{
    const std::string body = trim(s);

    SlicerVariant v;
    Storage& st = v.unique_storage();

    st.type = vector_type_of(elem);

    if (!body.empty())
    {
        std::vector<std::string_view> pieces;
        split_top_level(body, split, pieces);

        st.cells.reserve(pieces.size());

        for (size_t i = 0; i < pieces.size(); ++i)
        {
            const std::string piece = trim(pieces[i]);

            if (piece.empty() || (nil_keyword_applies(elem) && piece == g_nilString))
            {
                st.cells.push_back(Cell());
                continue;
            }

            const std::optional<Cell> c = parse_cell(elem, pieces[i]);

            if (!c)
                return std::nullopt;

            st.cells.push_back(*c);
        }
    }

    return v;
}

inline bool SlicerVariant::deserialize(const std::string& s, ValueType t)
{
    std::optional<SlicerVariant> parsed = is_vector_type(t)
        ? parse_vector(elem_type_of(t), s)
        : parse(t, s);

    if (!parsed)
        return false;

    *this = std::move(*parsed);
    return true;
}

inline std::string SlicerVariant::serialize_cell(const Cell& c, bool as_vector_elem, ValueType elem)
{
    if (std::holds_alternative<NilType>(c))
    {
        if (as_vector_elem && nil_keyword_applies(elem))
            return g_nilString;

        return std::string();
    }

    if (const bool* v = std::get_if<bool>(&c))
        return *v ? "1" : "0";

    if (const int64_t* v = std::get_if<int64_t>(&c))
        return int64_to_string(*v);

    if (const double* v = std::get_if<double>(&c))
        return format_double(*v);

    if (const Percent* v = std::get_if<Percent>(&c))
        return format_double(v->value) + "%";

    if (const FloatOrPercent* v = std::get_if<FloatOrPercent>(&c))
        return format_double(v->value) + (v->is_percent ? "%" : "");

    if (const std::string* s = std::get_if<std::string>(&c))
    {
        if (!as_vector_elem)
            return escape_cstyle(*s);

        return string_needs_quotes(*s) ? "\"" + escape_cstyle(*s) + "\"" : *s;
    }

    if (const Point* p = std::get_if<Point>(&c))
    {
        const char* sep = as_vector_elem ? "x" : ",";

        return format_double(p->x()) + sep + format_double(p->y());
    }

    if (const Point3* p = std::get_if<Point3>(&c))
    {
        const char* sep = as_vector_elem ? "x" : ",";

        return format_double(p->x()) + sep + format_double(p->y()) + sep + format_double(p->z());
    }

    if (const EnumValue* v = std::get_if<EnumValue>(&c))
        return int64_to_string(v->index);

    return std::string();
}

inline std::string SlicerVariant::serialize(const std::string& split) const
{
    const Storage& s = storage();

    if (!is_vector())
        return serialize_cell(scalar_ref(), false, elem_type_of(s.type));

    std::string out;

    for (size_t i = 0; i < s.cells.size(); ++i)
    {
        if (i)
            out += split;

        out += serialize_cell(s.cells[i], true, elem_type_of(s.type));
    }

    return out;
}

inline std::vector<std::string> SlicerVariant::serialize_vector() const
{
    std::vector<std::string> out;

    if (!is_vector())
        return out;

    const Storage& s = storage();

    out.reserve(s.cells.size());

    for (size_t i = 0; i < s.cells.size(); ++i)
        out.push_back(serialize_cell(s.cells[i], true, elem_type_of(s.type)));

    return out;
}

}

template<>
struct std::hash<ConfigSchema::SlicerVariant>
{
    size_t operator()(const ConfigSchema::SlicerVariant& v) const noexcept
    {
        return v.hash();
    }
};
