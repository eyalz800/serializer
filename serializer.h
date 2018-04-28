#ifndef __zpp_serializer_h__
#define __zpp_serializer_h__

#include <utility>
#include <cstdint>
#include <cstring>
#include <cstddef>
#include <vector>
#include <string>
#include <tuple>
#include <type_traits>
#include <array>
#include <new>
#include <unordered_map>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <mutex>
#include <algorithm>
#include <shared_mutex>

namespace zpp
{

/**
 * Supports serialization of objects and polymorphic objects.
 * Example of non polymorphic serialization:
 * ~~~
 * class point
 * {
 * public:
 *     point() = default;
 *     point(int x, int y) noexcept :
 *           m_x(x),
 *           m_y(y)
 *       {
 *       }
 *
 *     friend zpp::serializer::access;
 *     template <typename Archive, typename Self>
 *     static void serialize(Archive & archive, Self & self)
 *     {
 *         archive(self.m_x, self.m_y);
 *     }
 *
 *     int get_x() const noexcept
 *     {
 *           return m_x;
 *       }
 *    
 *       int get_y() const noexcept
 *       {
 *           return m_y;
 *       }
 *
 * private:
 *     int m_x = 0;
 *     int m_y = 0;
 * };
 *
 * static void foo()
 * {
 *     std::vector<unsigned char> data;
 *     zpp::serializer::memory_input_archive in(data);
 *     zpp::serializer::memory_output_archive out(data);
 *
 *     out(point(1337, 1338));
 *     
 *     point my_point;
 *     in(my_point);
 *
 *     std::cout << my_point.get_x() << ' ' << my_point.get_y() << '\n';
 * }
 * ~~~
 *
 * Example of polymorphic serialization:
 * ~~~
 * class person : public zpp::serializer::polymorphic
 * {
 * public:
 *     person() = default;
 *     explicit person(std::string name) noexcept :
 *           m_name(std::move(name))
 *       {
 *       }
 *
 *     friend zpp::serializer::access;
 *     template <typename Archive, typename Self>
 *     static void serialize(Archive & archive, Self & self)
 *     {
 *           archive(self.m_name);
 *       }
 *
 *       const std::string & get_name() const noexcept
 *       {
 *           return m_name;
 *       }
 *
 *     virtual void print() const
 *     {
 *         std::cout << "person: " << m_name;
 *     }
 *
 * private:
 *     std::string m_name;
 * };
 *
 * class student : public person
 * {
 * public:
 *     student() = default;
 *     student(std::string name, std::string university) noexcept :
 *         person(std::move(name)),
 *         m_university(std::move(university))
 *     {
 *     }
 *
 *     friend zpp::serializer::access;
 *     template <typename Archive, typename Self>
 *     static void serialize(Archive & archive, Self & self)
 *     {
 *         person::serialize(archive, self);
 *           archive(self.m_university);
 *       }
 *
 *     virtual void print() const
 *     {
 *         std::cout << "student: " << person::get_name() << ' ' << m_university << '\n';
 *     }
 *
 * private:
 *     std::string m_university;
 * };
 *
 * namespace
 * {
 * zpp::serializer::register_types<
 *    zpp::serializer::make_type<person, zpp::serializer::make_id("v1::person")>,
 *    zpp::serializer::make_type<student, zpp::serializer::make_id("v1::student")>
 * > _;
 * } // <anynymous namespace>
 * 
 * static void foo()
 * {
 *     std::vector<unsigned char> data;
 *     zpp::serializer::memory_input_archive in(data);
 *     zpp::serializer::memory_output_archive out(data);
 *
 *     std::unique_ptr<person> my_person = std::make_unique<student>("1337", "1337University");
 *     out(my_person);
 *
 *     my_person = nullptr;
 *     in(my_person);
 *
 *     my_person->print();
 * }
 *
 * static void bar()
 * {
 *     std::vector<unsigned char> data;
 *     zpp::serializer::memory_input_archive in(data);
 *     zpp::serializer::memory_output_archive out(data);
 *
 *     out(zpp::serializer::as_polymorphic(student("1337", "1337University")));
 *     
 *     std::unique_ptr<person> my_person;
 *     in(my_person);
 *
 *     my_person->print();
 * }
 * ~~~
 */
namespace serializer
{
namespace detail
{

/**
 * Map any sequence of types to void.
 */
template <typename...>
using void_t = void;

/**
 * Tests if all conditions are true, empty means true.
 * Example:
 * ~~~
 * all_of<true, false, true>::value == false
 * all_of<true, true>::value == true
 * all_of<false, false>::value == false
 * all_of<>::value == true
 * ~~~
 */
template <bool... bConditions>
struct all_of : std::true_type {};

template <bool... bConditions>
struct all_of<false, bConditions...> : std::false_type {};

template <bool... bConditions>
struct all_of<true, bConditions...> : all_of<bConditions...> {};

template <>
struct all_of<true> : std::true_type {};

/**
 * Remove const of container value_type
 */
template <typename Container, typename = void>
struct container_nonconst_value_type
{
    using type = std::remove_const_t<typename Container::value_type>;
};

/**
 * Same as above, except in case of std::map and std::unordered_map, and similar,
 * we also need to remove the const of the key type.
 */
template <template <typename...> class Container, typename KeyType, typename MappedType, typename... ExtraTypes>
struct container_nonconst_value_type<Container<KeyType, MappedType, ExtraTypes...>,
    void_t<
        // Require existence of key_type.
        typename Container<KeyType, MappedType, ExtraTypes...>::key_type,

        // Require existence of mapped_type.
        typename Container<KeyType, MappedType, ExtraTypes...>::mapped_type,

        // Require that the value type is a pair of const KeyType and MappedType.
        std::enable_if_t<std::is_same<
            std::pair<const KeyType, MappedType>,
            typename Container<KeyType, MappedType, ExtraTypes...>::value_type
        >::value>
    >
>
{
    using type = std::pair<KeyType, MappedType>;
};

/**
 * Alias to the above.
 */
template <typename Container>
using container_nonconst_value_type_t = typename container_nonconst_value_type<Container>::type;

/**
 * The serializer exception template.
 */
template <typename Base, std::size_t Id>
class exception : public Base
{
public:
    /**
     * Use the constructors from the base class.
     */
    using Base::Base;

    /**
     * Constructs an exception object with empty message.
     */
    exception() :
        Base(std::string{})
    {
    }
};

/**
 * A no operation, single byte has same representation in little/big endian.
 */
inline constexpr std::uint8_t swap_byte_order(std::uint8_t value) noexcept
{
    return value;
}

/**
 * Swaps the byte order of a given integer.
 */
inline constexpr std::uint16_t swap_byte_order(std::uint16_t value) noexcept
{
    return (std::uint16_t(swap_byte_order(std::uint8_t(value))) << 8) |
        (swap_byte_order(std::uint8_t(value >> 8)));
}

/**
 * Swaps the byte order of a given integer.
 */
inline constexpr std::uint32_t swap_byte_order(std::uint32_t value) noexcept
{
    return (std::uint32_t(swap_byte_order(std::uint16_t(value))) << 16) |
        (swap_byte_order(std::uint16_t(value >> 16)));
}

/**
 * Swaps the byte order of a given integer.
 */
inline constexpr std::uint64_t swap_byte_order(std::uint64_t value) noexcept
{
    return (std::uint64_t(swap_byte_order(std::uint32_t(value))) << 32) |
        (swap_byte_order(std::uint32_t(value >> 32)));
}

/**
 * Rotates the given number left by count bits.
 */
template <typename Integer>
constexpr auto rotate_left(Integer number, std::size_t count)
{
    return (number << count) | (number >> ((sizeof(number) * 8) - count));
}

/**
 * Checks if has 'data()' member function.
 */
template <typename Type, typename = void>
struct has_data_member_function : std::false_type {};

/**
 * Checks if has 'data()' member function.
 */
template <typename Type>
struct has_data_member_function<Type, void_t<decltype(std::declval<Type &>().data())>> : std::true_type {};

} // detail

/**
 * @name Exceptions
 * @{
 */
using out_of_range = detail::exception<std::out_of_range, 0>;
using undeclared_polymorphic_type_error = detail::exception<std::runtime_error, 1>;
using attempt_to_serialize_null_pointer_error = detail::exception<std::logic_error, 2>;
using polymorphic_type_mismatch_error = detail::exception<std::runtime_error, 3>;
/**
 * @}
 */

/**
 * If C++17 or greater, use shared mutex, else, use shared timed mutex.
 */
#if __cplusplus > 201402L
/**
 * The shared mutex type, defined to shared mutex when available.
 */
using shared_mutex = std::shared_mutex;
#else
/**
 * The shared mutex type, defined to shared timed mutex when shared mutex is not available.
 */
using shared_mutex = std::shared_timed_mutex;
#endif

/**
 * The base class for polymorphic serialization.
 */
class polymorphic
{
public:
    /**
     * Pure virtual destructor, in order to become abstract
     * and make derived classes polymorphic.
     */
    virtual ~polymorphic() = 0;
};

/**
 * Default implementation for the destructor.
 */
inline polymorphic::~polymorphic() = default;

/**
 * Allow serialization with saving (output) archives, of objects held by reference,
 * that will be serialized as polymorphic, meaning, with leading polymorphic serialization id.
 */
template <typename Type>
class polymorphic_wrapper
{
public:
    static_assert(std::is_base_of<polymorphic, Type>::value,
            "The given type is not derived from polymorphic");

    /**
     * Constructs from the given object to be serialized as polymorphic.
     */
    explicit polymorphic_wrapper(const Type & object) noexcept :
        m_object(object)
    {
    }

    /**
     * Returns the object to be serialized as polymorphic.
     */
    const Type & operator*() const noexcept
    {
        return m_object;
    }

private:
    /**
     * The object to be serialized as polymorphic.
     */
    const Type & m_object;
}; // polymorphic_wrapper

/**
 * A facility to save object with leading polymorphic serialization id.
 */
template <typename Type>
auto as_polymorphic(const Type & object) noexcept
{
    return polymorphic_wrapper<Type>(object);
}

/**
 * The size type of the serializer.
 * It is used to indicate the size for containers.
 */
using size_type = std::uint32_t;

/**
 * The serialization id type,
 */
using id_type = std::uint64_t;

/**
 * This class grants the serializer access to the serialized types.
 */
class access
{
public:
    /**
     * Allows placement construction of types.
     */
    template <typename Item, typename... Arguments>
    static auto placement_new(void * pAddress, Arguments && ... arguments) noexcept(
        noexcept(Item(std::forward<Arguments>(arguments)...)))
    {
        return ::new (pAddress) Item(std::forward<Arguments>(arguments)...);
    }

    /**
     * Allows dynamic construction of types.
     * This overload is for non polymorphic serialization.
     */
    template <typename Item, typename... Arguments,
        typename = std::enable_if_t<!std::is_base_of<polymorphic, Item>::value>
    >
    static auto make_unique(Arguments && ... arguments)
    {
        // Construct the requested type, using new since constructor might be private.
        return std::unique_ptr<Item>(new Item(std::forward<Arguments>(arguments)...));
    }

    /**
     * Allows dynamic construction of types.
     * This overload is for polymorphic serialization.
     */
    template <typename Item, typename... Arguments,
        typename = std::enable_if_t<std::is_base_of<polymorphic, Item>::value>,
        typename = void
    >
    static auto make_unique(Arguments && ... arguments)
    {
        // We create a deleter that will delete using the base class polymorphic which, as we declared,
        // has a public virtual destructor.
        struct deleter
        {
            void operator()(Item * item) noexcept
            {
                delete static_cast<polymorphic *>(item);
            }
        };

        // Construct the requested type, using new since constructor might be private.
        return std::unique_ptr<Item, deleter>(new Item(std::forward<Arguments>(arguments)...));
    }

    /**
     * Allows destruction of types.
     */
    template <typename Item>
    static void destruct(Item & item) noexcept
    {
        item.~Item();
    }
}; // access

/**
 * Enables serialization of arbitrary binary data.
 * Use only with care.
 */
template <typename Item>
class binary
{
public:
    /**
     * Constructs the binary wrapper from pointer and count of items.
     */
    binary(Item * items, size_type count) :
        m_items(items),
        m_count(count)
    {
    }

    /**
     * Returns a pointer to the first item.
     */
    Item * data() const noexcept
    {
        return m_items;
    }

    /**
     * Returns the size in bytes of the binary data.
     */
    size_type size_in_bytes() const noexcept
    {
        return m_count * sizeof(Item);
    }

    /**
     * Returns the count of items in the binary wrapper.
     */
    size_type count() const noexcept
    {
        return m_count;
    }

private:
    /**
     * Pointer to the items.
     */
    Item * m_items = nullptr;

    /**
     * The number of items.
     */
    size_type m_count = 0;
};

/**
 * Allows serialization as binary data.
 * Use only with care.
 */
template <typename Item>
binary<Item> as_binary(Item * item, size_type count)
{
    static_assert(std::is_trivially_copyable<Item>::value,
           "Must be trivially copyable");
    
    return { item, count };
}

/**
 * Allows serialization as binary data.
 * Use only with care.
 */
inline binary<unsigned char> as_binary(void * data, size_type size)
{
    return { static_cast<unsigned char *>(data), size };
}

/**
 * Allows serialization as binary data.
 */
inline binary<const unsigned char> as_binary(const void * data, size_type size)
{
    return { static_cast<const unsigned char *>(data), size };
}

/**
 * The serialization method type.
 */
template <typename Archive, typename = void>
struct serialization_method;

/**
 * The serialization method type exporter, for loading (input) archives.
 */
template <typename Archive>
struct serialization_method<Archive, typename Archive::loading>
{
    /**
     * Disabled default constructor.
     */
    serialization_method() = delete;

    /**
     * The exported type.
     */
    using type = void(*)(Archive &, std::unique_ptr<polymorphic> &);
}; // serialization_method

/**
 * The serialization method type exporter, for saving (output) archives.
 */
template <typename Archive>
struct serialization_method<Archive, typename Archive::saving>
{
    /**
     * Disabled default constructor.
     */
    serialization_method() = delete;

    /**
     * The exported type.
     */
    using type = void(*)(Archive &, const polymorphic &);
}; // serialization_method

/**
 * The serialization method type.
 */
template <typename Archive>
using serialization_method_t = typename serialization_method<Archive>::type;

/**
 * Make a serialization method from type and a loading (input) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = typename Archive::loading
>
serialization_method_t<Archive> make_serialization_method() noexcept
{
    return [](Archive & archive, std::unique_ptr<polymorphic> & object) {
        auto concrete_type = access::make_unique<Type>();
        archive(*concrete_type);
        object.reset(concrete_type.release());
    };
}

/**
 * Make a serialization method from type and a saving (output) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = typename Archive::saving,
    typename = void
>
serialization_method_t<Archive> make_serialization_method() noexcept
{
    return [](Archive & archive, const polymorphic & object) {
        archive(dynamic_cast<const Type &>(object));
    };
}

/**
 * This is the base archive of the serializer.
 * It enables saving and loading items into/from the archive, via operator().
 */
template <typename ArchiveType>
class archive
{
public:
    /**
     * The derived archive type.
     */
    using archive_type = ArchiveType;

    /**
     * Save/Load the given items into/from the archive.
     */
    template <typename... Items>
    void operator()(Items && ... items)
    {
        // Disallow serialization of pointer types.
        static_assert(detail::all_of<!std::is_pointer<std::remove_reference_t<Items>>::value...>::value,
            "Serialization of pointer types is not allowed");

        // Serialize the items.
        serialize_items(std::forward<Items>(items)...);
    }

protected:
    /**
     * Constructs the archive.
     */
    archive() = default;

    /**
     * Protected destructor to allow safe public inheritance.
     */
    ~archive() = default;

private:
    /**
     * Serialize the given items, one by one.
     */
    template <typename Item, typename... Items>
    void serialize_items(Item && first, Items && ... items)
    {
        // Invoke serialize_item the first item.
        serialize_item(std::forward<Item>(first));

        // Serialize the rest of the items.
        serialize_items(std::forward<Items>(items)...);
    }

    /**
     * Serializes zero items.
     */
    void serialize_items()
    {
    }
    
    /**
     * Serialize a single item.
     * This overload is for class type items with serialize method.
     */
    template <typename Item, typename...,
        typename = decltype(std::remove_reference_t<Item>::serialize(
            std::declval<archive_type &>(), std::declval<Item &>()))
    >
    void serialize_item(Item && item)
    {
        // Forward as lvalue.
        std::remove_reference_t<Item>::serialize(concrete_archive(), item);
    }

    /**
     * Serialize a single item.
     * This overload is for types with outer serialize method.
     */
    template <typename Item, typename...,
        typename = decltype(serialize(std::declval<archive_type &>(), std::declval<Item &>())),
        typename = void
    >
    void serialize_item(Item && item)
    {
        // Forward as lvalue.
        serialize(concrete_archive(), item);
    }

    /**
     * Serialize a single item.
     * This overload is for fundamental types.
     */
    template <typename Item, typename...,
        typename = std::enable_if_t<std::is_fundamental<std::remove_reference_t<Item>>::value>,
        typename = void, typename = void
    >
    void serialize_item(Item && item)
    {
        // Forward as lvalue.
        concrete_archive().serialize(item);
    }

    /**
     * Serialize a single item.
     * This overload is for enum classes.
     */
    template <typename Item, typename...,
        typename = std::enable_if_t<std::is_enum<std::remove_reference_t<Item>>::value>,
        typename = void, typename = void, typename = void
    >
    void serialize_item(Item && item)
    {
        // If the enum is const, we want the type to be a const type, else non-const.
        using integral_type = std::conditional_t<
            std::is_const<std::remove_reference_t<Item>>::value,
            const std::underlying_type_t<std::remove_reference_t<Item>>,
            std::underlying_type_t<std::remove_reference_t<Item>>
        >;

        // Cast the enum to the underlying type, and forward as lvalue.
        concrete_archive().serialize(reinterpret_cast<std::add_lvalue_reference_t<integral_type>>(item));
    }

    /**
     * Serialize binary data.
     */
    template <typename T>
    void serialize_item(binary<T> && item)
    {
        concrete_archive().serialize(item.data(), item.size_in_bytes());
    }

    /**
     * Returns the concrete archive.
     */
    archive_type & concrete_archive()
    {
        return static_cast<archive_type &>(*this);
    }
}; // archive

/**
 * This archive serves as an output archive, which saves data into memory.
 * Every save operation appends data into the vector.
 * This archive serves as an optimization around vector, use 'memory_output_archive' instead.
 */
class lazy_vector_memory_output_archive : public archive<lazy_vector_memory_output_archive>
{
public:
    /**
     * The base archive.
     */
    using base = archive<lazy_vector_memory_output_archive>;

    /**
     * Declare base as friend.
     */
    friend base;

    /**
     * saving archive.
     */
    using saving = void;

protected:
    /**
     * Constructs a memory output archive, that outputs to the given vector.
     */
    explicit lazy_vector_memory_output_archive(std::vector<unsigned char> & output) noexcept :
        m_output(std::addressof(output)),
        m_size(output.size())
    {
    }

    /**
     * Serialize a single item - save it to the vector.
     */
    template <typename Item>
    void serialize(Item && item)
    {
        // Increase vector size.
        if (m_size + sizeof(item) > m_output->size()) {
             m_output->resize((m_size + sizeof(item)) * 3 / 2);
        }

        // Copy the data to the end of the vector.
        std::copy_n(reinterpret_cast<const unsigned char *>(std::addressof(item)),
            sizeof(item),
            m_output->data() + m_size);

        // Increase the size.
        m_size += sizeof(item);
    }

    /**
     * Serialize binary data - save it to the vector.
     */
    void serialize(const void * data, size_type size)
    {
        // Increase vector size.
        if (m_size + size > m_output->size()) {
             m_output->resize((m_size + size) * 3 / 2);
        }

        // Copy the data to the end of the vector.
        std::copy_n(static_cast<const unsigned char *>(data),
            size,
            m_output->data() + m_size);

        // Increase the size.
        m_size += size;
    }

     /**
      * Resizes the vector to the desired size.
      */
     void fit_vector()
     {
          m_output->resize(m_size);
     }

private:
    /**
     * The output vector.
     */
    std::vector<unsigned char> * m_output{};

     /**
      * The vector size.
      */
     std::size_t m_size{};
}; // lazy_vector_memory_output_archive

/**
 * This archive serves as an output archive, which saves data into memory.
 * Every save operation appends data into the vector.
 */
class memory_output_archive : private lazy_vector_memory_output_archive
{
public:
    /**
     * The base archive.
     */
    using base = lazy_vector_memory_output_archive;

    /**
     * Constructs a memory output archive, that outputs to the given vector.
     */
    explicit memory_output_archive(std::vector<unsigned char> & output) noexcept :
          lazy_vector_memory_output_archive(output)
    {
    }

    /**
     * Saves items into the archive.
     */
    template <typename... Items>
    void operator()(Items && ... items)
     {
          try {
               // Serialize the items.
               base::operator()(std::forward<Items>(items)...);

               // Fit the vector.
               fit_vector();
          } catch (...) {
               // Fit the vector.
               fit_vector();
               throw;
          }
     }
};

/**
 * This archive serves as the memory view input archive, which loads data from non owning memory.
 * Every load operation advances an offset to that the next data may be loaded on the next iteration.
 */
class memory_view_input_archive : public archive<memory_view_input_archive>
{
public:
    /**
     * The base archive.
     */
    using base = archive<memory_view_input_archive>;

    /**
     * Declare base as friend.
     */
    friend base;

    /**
     * Loading archive.
     */
    using loading = void;

    /**
     * Construct a memory view input archive, that loads data from an array of given pointer and size.
     */
    memory_view_input_archive(const unsigned char * input, std::size_t size) noexcept :
        m_input(input),
        m_size(size)
    {
    }

    /**
     * Construct a memory view input archive, that loads data from an array of given pointer and size.
     */
    memory_view_input_archive(const char * input, std::size_t size) noexcept :
        m_input(reinterpret_cast<const unsigned char *>(input)),
        m_size(size)
    {
    }

protected:
    /**
     * Resets the serialization to offset zero.
     */
    void reset() noexcept
    {
        m_offset = 0;
    }

    /**
     * Returns the offset of the serialization.
     */
    std::size_t get_offset() const noexcept
    {
        return m_offset;
    }
    
    /**
     * Serialize a single item - load it from the vector.
     */
    template <typename Item>
    void serialize(Item && item)
    {
        // Verify that the vector is large enough to contain the item.
        if (m_size < (sizeof(item) + m_offset)) {
            throw out_of_range("Input vector was not large enough to contain the requested item");
        }

        // Fetch the item from the vector.
        std::copy_n(m_input + m_offset, sizeof(item), reinterpret_cast<unsigned char *>(std::addressof(item)));

        // Increase the offset according to item size.
        m_offset += sizeof(item);
    }

    /**
     * Serializes binary data.
     */
    void serialize(void * data, size_type size)
    {
        // Verify that the vector is large enough to contain the data.
        if (m_size < (size + m_offset)) {
            throw out_of_range("Input vector was not large enough to contain the requested item");
        }

        // Fetch the binary data from the vector.
        std::copy_n(m_input + m_offset, size, static_cast<unsigned char *>(data));

        // Increase the offset according to data size.
        m_offset += size;
    }

private:
    /**
     * The input data.
     */
    const unsigned char * m_input{};

    /**
     * The input size.
     */
    std::size_t m_size{};

    /**
     * The next input.
     */
    std::size_t m_offset{};
}; // memory_view_input_archive

/**
 * This archive serves as the memory input archive, which loads data from owning memory.
 * Every load operation erases data from the beginning of the vector.
 */
class memory_input_archive : private memory_view_input_archive
{
public:
    /**
     * Construct a memory input archive from a vector.
     */
    memory_input_archive(std::vector<unsigned char> & input) :
        memory_view_input_archive(input.data(), input.size()),
        m_input(std::addressof(input))
    {
    }

    /**
     * Load items from the archive.
     */
    template <typename... Items>
    void operator()(Items && ... items)
    {
        try {
            // Update the input archive.
            static_cast<memory_view_input_archive &>(*this) = { m_input->data(), m_input->size() };

            // Load the items.
            memory_view_input_archive::operator()(std::forward<Items>(items)...);
        } catch (...) {
            // Erase the loaded elements.
            m_input->erase(m_input->begin(), m_input->begin() + get_offset());

            // Reset to offset zero.
            reset();
            throw;
        }

        // Erase the loaded elements.
        m_input->erase(m_input->begin(), m_input->begin() + get_offset());

        // Reset to offset zero.
        reset();
    }

private:
    /**
     * The input data.
     */
    std::vector<unsigned char> * m_input{};
};

/**
 * This class manages polymorphic type registration for serialization process.
 */
template <typename Archive>
class registry
{
public:
    static_assert(!std::is_reference<Archive>::value,
        "Disallows reference type for archive in registry");

    /**
     * Returns the global instance of the registry.
     */
    static registry & get_instance() noexcept
    {
        static registry registry;
        return registry;
    }

    /**
     * Add a serialization method for a given polymorphic type and id.
     */
    template <typename Type, id_type id>
    void add()
    {
        add<Type>(id);
    }

    /**
     * Adds a serialization method for a given polymorphic type and id.
     */
    template <typename Type>
    void add(id_type id)
    {
        add(id, typeid(Type).name(), make_serialization_method<Archive, Type>());
    }

    /**
     * Add a serialization method for a given polymorphic type information string and id.
     * The behavior is undefined if the type isn't derived from polymorphic.
     */
    void add(id_type id, std::string type_information_string, serialization_method_t<Archive> serialization_method)
    {
        // Lock the serialization method maps for write access.
        std::lock_guard<shared_mutex> lock(m_shared_mutex);

        // Add the serialization id to serialization method mapping.
        m_serialization_id_to_method.emplace(id, std::move(serialization_method));

        // Add the type information to to serialization id mapping.
        m_type_information_to_serialization_id.emplace(std::move(type_information_string), id);
    }

    /**
     * Serialize a polymorphic type, in case of a loading (input) archive.
     */
    template <typename...,
        typename ArchiveType = Archive,
        typename = typename ArchiveType::loading
    >
    void serialize(Archive & archive, std::unique_ptr<polymorphic> & object)
    {
        id_type id = 0;

        // Load the serialization id.
        archive(id);

        // Lock the serialization method maps for read access.
        std::shared_lock<shared_mutex> lock(m_shared_mutex);
        
        // Find the serialization method.
        auto serialization_id_to_method_pair = m_serialization_id_to_method.find(id);
        if (m_serialization_id_to_method.end() == serialization_id_to_method_pair) {
            throw undeclared_polymorphic_type_error();
        }

        // Fetch the serialization method.
        auto serialization_method = serialization_id_to_method_pair->second;

        // Unlock the serialization method maps.
        lock.unlock();

        // Serialize (load) the given object.
        serialization_method(archive, object);
    }

    /**
     * Serialize a polymorphic type, in case of a saving (output) archive.
     */
    template <typename...,
        typename ArchiveType = Archive,
        typename = typename ArchiveType::saving
    >
    void serialize(Archive & archive, const polymorphic & object)
    {
        // Lock the serialization method maps for read access.
        std::shared_lock<shared_mutex> lock(m_shared_mutex);

        // Find the serialization id.
        auto type_information_to_serialization_id_pair = m_type_information_to_serialization_id.find(
            typeid(object).name());
        if (m_type_information_to_serialization_id.end() == type_information_to_serialization_id_pair) {
            throw undeclared_polymorphic_type_error();
        }

        // Fetch the serialization id.
        auto id = type_information_to_serialization_id_pair->second;

        // Find the serialization method.
        auto serialization_id_to_method_pair = m_serialization_id_to_method.find(id);
        if (m_serialization_id_to_method.end() == serialization_id_to_method_pair) {
            throw undeclared_polymorphic_type_error();
        }

        // Fetch the serialization method.
        auto serialization_method = serialization_id_to_method_pair->second;

        // Unlock the serialization method maps.
        lock.unlock();

        // Serialize (save) the serialization id.
        archive(id);

        // Serialize (save) the given object.
        serialization_method(archive, object);
    }

private:
    /**
     * Default constructor, defaulted.
     */
    registry() = default;

private:
    /**
     * The shared mutex that protects the maps below.
     */
    shared_mutex m_shared_mutex;

    /**
     * A map between serialization id to method.
     */
    std::unordered_map<id_type, serialization_method_t<Archive>> m_serialization_id_to_method;

    /**
     * A map between type information string to serialization id.
     */
    std::unordered_map<std::string, id_type> m_type_information_to_serialization_id;
}; // registry

/**
 * Serialize resizable containers, operates on loading (input) archives.
 */
template <typename Archive, typename Container, typename...,
    typename = decltype(std::declval<Container &>().size()),
    typename = decltype(std::declval<Container &>().begin()),
    typename = decltype(std::declval<Container &>().end()),
    typename = decltype(std::declval<Container &>().resize(std::size_t())),
    typename = std::enable_if_t<
        std::is_class<
            typename Container::value_type
        >::value || !std::is_base_of<
            std::random_access_iterator_tag,
            typename std::iterator_traits<typename Container::iterator>::iterator_category
        >::value
    >,
    typename = typename Archive::loading,
    typename = void, typename = void, typename = void, typename = void
>
void serialize(Archive & archive, Container & container)
{
    size_type size = 0;

    // Fetch the number of items to load.
    archive(size);

    // Resize the container to match the size.
    container.resize(size);

    // Serialize all the items.
    for (auto & item : container) {
        archive(item);
    }
};

/**
 * Serialize resizable containers, operates on saving (output) archives.
 */
template <typename Archive, typename Container, typename...,
    typename = decltype(std::declval<Container &>().size()),
    typename = decltype(std::declval<Container &>().begin()),
    typename = decltype(std::declval<Container &>().end()),
    typename = decltype(std::declval<Container &>().resize(std::size_t())),
    typename = std::enable_if_t<
        std::is_class<
            typename Container::value_type
        >::value || !std::is_base_of<
            std::random_access_iterator_tag,
            typename std::iterator_traits<typename Container::iterator>::iterator_category
        >::value || !detail::has_data_member_function<Container>::value
    >,
    typename = typename Archive::saving,
    typename = void, typename = void, typename = void, typename = void
>
void serialize(Archive & archive, const Container & container)
{
    // Save the container size.
    archive(static_cast<size_type>(container.size()));

    // Serialize all the items.
    for (auto & item : container) {
        archive(item);
    }
}

/**
 * Serialize resizable, continuous containers, of fundamental or enumeration types.
 * Operates on loading (input) archives.
 */
template <typename Archive, typename Container, typename...,
    typename = decltype(std::declval<Container &>().size()),
    typename = decltype(std::declval<Container &>().begin()),
    typename = decltype(std::declval<Container &>().end()),
    typename = decltype(std::declval<Container &>().resize(std::size_t())),
    typename = decltype(std::declval<Container &>().data()),
    typename = std::enable_if_t<
        std::is_fundamental<typename Container::value_type>::value ||
        std::is_enum<typename Container::value_type>::value
    >,
    typename = std::enable_if_t<std::is_base_of<
        std::random_access_iterator_tag,
        typename std::iterator_traits<typename Container::iterator>::iterator_category>::value
    >,
    typename = typename Archive::loading,
    typename = void, typename = void, typename = void, typename = void
>
void serialize(Archive & archive, Container & container)
{
    size_type size = 0;

    // Fetch the number of items to load.
    archive(size);

    // Resize the container to match the size.
    container.resize(size);

    // If the size is zero, return.
    if (!size) {
        return;
    }

    // Serialize the binary data.
    archive(as_binary(std::addressof(container[0]), static_cast<size_type>(container.size())));
};

/**
 * Serialize resizable, continuous containers, of fundamental or enumeration types.
 * Operates on saving (output) archives.
 */
template <typename Archive, typename Container, typename...,
    typename = decltype(std::declval<Container &>().size()),
    typename = decltype(std::declval<Container &>().begin()),
    typename = decltype(std::declval<Container &>().end()),
    typename = decltype(std::declval<Container &>().resize(std::size_t())),
    typename = decltype(std::declval<Container &>().data()),
    typename = std::enable_if_t<
        std::is_fundamental<typename Container::value_type>::value ||
        std::is_enum<typename Container::value_type>::value
    >,
    typename = std::enable_if_t<std::is_base_of<
        std::random_access_iterator_tag,
        typename std::iterator_traits<typename Container::iterator>::iterator_category>::value
    >,
    typename = typename Archive::saving,
    typename = void, typename = void, typename = void, typename = void
>
void serialize(Archive & archive, const Container & container)
{
    // The container size.
    auto size = static_cast<size_type>(container.size());

    // Save the container size.
    archive(size);

    // If the size is zero, return.
    if (!size) {
        return;
    }

    // Serialize the binary data.
    archive(as_binary(std::addressof(container[0]), static_cast<size_type>(container.size())));
}

/**
 * Serialize Associative and UnorderedAssociative containers, operates on loading (input) archives.
 */
template <typename Archive, typename Container, typename...,
    typename = decltype(std::declval<Container &>().size()),
    typename = decltype(std::declval<Container &>().begin()),
    typename = decltype(std::declval<Container &>().end()),
    typename = typename Container::value_type,
    typename = typename Container::key_type,
    typename = typename Archive::loading
>
void serialize(Archive & archive, Container & container)
{
    size_type size = 0;

    // Fetch the number of items to load.
    archive(size);

    // Serialize all the items.
    for (size_type i = 0; i < size; ++i) {
        // Deduce the container item type.
        using item_type = detail::container_nonconst_value_type_t<Container>;

        // Create just enough storage properly aligned for one item.
        std::aligned_storage_t<sizeof(item_type), alignof(item_type)> storage;

        // Default construct the item in the storage.
        auto item = access::placement_new<item_type>(std::addressof(storage));

        try {
            // Serialize the item.
            archive(*item);

            // Insert the item to the container.
            container.insert(std::move(*item));
        } catch (...) {
            // Destruct the item.
            access::destruct(*item);
            throw;
        }

        // Destruct the item.
        access::destruct(*item);
    }
}

/**
 * Serialize Associative and UnorderedAssociative containers, operates on saving (output) archives.
 */
template <typename Archive, typename Container, typename...,
    typename = decltype(std::declval<Container &>().size()),
    typename = decltype(std::declval<Container &>().begin()),
    typename = decltype(std::declval<Container &>().end()),
    typename = typename Container::value_type,
    typename = typename Container::key_type,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const Container & container)
{
    // Save the container size.
    archive(static_cast<size_type>(container.size()));

    // Serialize every item.
    for (auto & item : container) {
        archive(item);
    }
}

/**
 * Serialize arrays, operates on loading (input) archives.
 */
template <typename Archive, typename Item, std::size_t size, typename...,
    typename = typename Archive::loading
>
void serialize(Archive & archive, Item(&array)[size])
{
    // Serialize every item.
    for (auto & item: array) {
        archive(item);
    }
}

/**
 * Serialize arrays, operates on saving (output) archives.
 */
template <typename Archive, typename Item, std::size_t size, typename...,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const Item(&array)[size])
{
    // Serialize every item.
    for (auto & item: array) {
        archive(item);
    }
}

/**
 * Serialize std::array, operates on loading (input) archives.
 */
template <typename Archive, typename Item, std::size_t size, typename...,
    typename = typename Archive::loading
>
void serialize(Archive & archive, std::array<Item, size> & array)
{
    // Serialize every item.
    for (auto & item: array) {
        archive(item);
    }
}

/**
 * Serialize std::array, operates on saving (output) archives.
 */
template <typename Archive, typename Item, std::size_t size, typename...,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const std::array<Item, size> & array)
{
    // Serialize every item.
    for (auto & item: array) {
        archive(item);
    }
}

/**
 * Serialize std::pair, operates on loading (input) archives.
 */
template <typename Archive, typename First, typename Second, typename...,
    typename = typename Archive::loading
>
void serialize(Archive & archive, std::pair<First, Second> & pair)
{
    // Serialize first, then second.
    archive(pair.first, pair.second);
}

/**
 * Serialize std::pair, operates on saving (output) archives.
 */
template <typename Archive, typename First, typename Second, typename...,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const std::pair<First, Second> & pair)
{
    // Serialize first, then second.
    archive(pair.first, pair.second);
}

/**
 * Serialize std::tuple, operates on loading (input) archives.
 */
template <typename Archive, typename... TupleItems,
    typename = typename Archive::loading
>
void serialize(Archive & archive, std::tuple<TupleItems...> & tuple)
{
    // Delegate to a helper function with an index sequence.
    serialize(archive, tuple, std::make_index_sequence<sizeof...(TupleItems)>());
}

/**
 * Serialize std::tuple, operates on saving (output) archives.
 */
template <typename Archive, typename... TupleItems,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const std::tuple<TupleItems...> & tuple)
{
    // Delegate to a helper function with an index sequence.
    serialize(archive, tuple, std::make_index_sequence<sizeof...(TupleItems)>());
}

/**
 * Serialize std::tuple, operates on loading (input) archives.
 * This overload serves as a helper function that accepts an index sequence.
 */
template <typename Archive, typename... TupleItems, std::size_t... Indices,
    typename = typename Archive::loading
>
void serialize(Archive & archive, std::tuple<TupleItems...> & tuple, std::index_sequence<Indices...>)
{
    archive(std::get<Indices>(tuple)...);
}

/**
 * Serialize std::tuple, operates on saving (output) archives.
 * This overload serves as a helper function that accepts an index sequence.
 */
template <typename Archive, typename... TupleItems, std::size_t... Indices,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const std::tuple<TupleItems...> & tuple, std::index_sequence<Indices...>)
{
    archive(std::get<Indices>(tuple)...);
}

/**
 * Serialize std::unique_ptr of non polymorphic, in case of a loading (input) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = std::enable_if_t<!std::is_base_of<polymorphic, Type>::value>,
    typename = typename Archive::loading
>
void serialize(Archive & archive, std::unique_ptr<Type> & object)
{
    // Construct a new object.
    auto loaded_object = access::make_unique<Type>();

    // Serialize the object.
    archive(*loaded_object);

    // Transfer the object.
    object.reset(loaded_object.release());
}

/**
 * Serialize std::unique_ptr of non polymorphic, in case of a saving (output) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = std::enable_if_t<!std::is_base_of<polymorphic, Type>::value>,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const std::unique_ptr<Type> & object)
{
    // Prevent serialization of null pointers.
    if (nullptr == object) {
        throw attempt_to_serialize_null_pointer_error();
    }

    // Serialize the object.
    archive(*object);
}

/**
 * Serialize std::unique_ptr of polymorphic, in case of a loading (input) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = std::enable_if_t<std::is_base_of<polymorphic, Type>::value>,
    typename = typename Archive::loading,
    typename = void
>
void serialize(Archive & archive, std::unique_ptr<Type> & object)
{
    std::unique_ptr<polymorphic> loaded_type;

    // Get the instance of the polymorphic registry.
    auto & registry_instance = registry<Archive>::get_instance();

    // Serialize the object using the registry.
    registry_instance.serialize(archive, loaded_type);

    try {
        // Check if the loaded type is convertible to Type.
        object.reset(&dynamic_cast<Type &>(*loaded_type));

        // Release the object.
        loaded_type.release();
    } catch (const std::bad_cast &) {
        // The loaded type was not convertible to Type.
        throw polymorphic_type_mismatch_error();
    }
}

/**
 * Serialize std::unique_ptr of polymorphic, in case of a saving (output) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = std::enable_if_t<std::is_base_of<polymorphic, Type>::value>,
    typename = typename Archive::saving,
    typename = void
>
void serialize(Archive & archive, const std::unique_ptr<Type> & object)
{
    // Prevent serialization of null pointers.
    if (nullptr == object) {
        throw attempt_to_serialize_null_pointer_error();
    }

    // Get the instance of the polymorphic registry.
    auto & registry_instance = registry<Archive>::get_instance();

    // Serialize the object using the registry.
    registry_instance.serialize(archive, *object);
}

/**
 * Serialize std::shared_ptr of non polymorphic, in case of a loading (input) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = std::enable_if_t<!std::is_base_of<polymorphic, Type>::value>,
    typename = typename Archive::loading
>
void serialize(Archive & archive, std::shared_ptr<Type> & object)
{
    // Construct a new object.
    auto loaded_object = access::make_unique<Type>();

    // Serialize the object.
    archive(*loaded_object);

    // Transfer the object.
    object.reset(loaded_object.release());
}

/**
 * Serialize std::shared_ptr of non polymorphic, in case of a saving (output) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = std::enable_if_t<!std::is_base_of<polymorphic, Type>::value>,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const std::shared_ptr<Type> & object)
{
    // Prevent serialization of null pointers.
    if (nullptr == object) {
        throw attempt_to_serialize_null_pointer_error();
    }

    // Serialize the object.
    archive(*object);
}

/**
 * Serialize std::shared_ptr of polymorphic, in case of a loading (input) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = std::enable_if_t<std::is_base_of<polymorphic, Type>::value>,
    typename = typename Archive::loading,
    typename = void
>
void serialize(Archive & archive, std::shared_ptr<Type> & object)
{
    std::unique_ptr<polymorphic> loaded_type;

    // Get the instance of the polymorphic registry.
    auto & registry_instance = registry<Archive>::get_instance();

    // Serialize the object using the registry.
    registry_instance.serialize(archive, loaded_type);

    try {
        // Check if the loaded type is convertible to Type.
        object.reset(&dynamic_cast<Type &>(*loaded_type));

        // Release the object.
        loaded_type.release();
    } catch (const std::bad_cast &) {
        // The loaded type was not convertible to Type.
        throw polymorphic_type_mismatch_error();
    }
}

/**
 * Serialize std::shared_ptr of polymorphic, in case of a saving (output) archive.
 */
template <typename Archive, typename Type, typename...,
    typename = std::enable_if_t<std::is_base_of<polymorphic, Type>::value>,
    typename = typename Archive::saving,
    typename = void
>
void serialize(Archive & archive, const std::shared_ptr<Type> & object)
{
    // Prevent serialization of null pointers.
    if (nullptr == object) {
        throw attempt_to_serialize_null_pointer_error();
    }

    // Get the instance of the polymorphic registry.
    auto & registry_instance = registry<Archive>::get_instance();

    // Serialize the object using the registry.
    registry_instance.serialize(archive, *object);
}

/**
 * Serialize types wrapped with polymorphic_wrapper,
 * which is supported only for saving (output) archives.
 * Usually used with the as_polymorphic facility.
 */
template <typename Archive, typename Type, typename...,
    typename = typename Archive::saving
>
void serialize(Archive & archive, const polymorphic_wrapper<Type> & object)
{
    // Get the instance of the polymorphic registry.
    auto & registry_instance = registry<Archive>::get_instance();

    // Serialize using the registry.
    registry_instance.serialize(archive, *object);
}

/**
 * A meta container that holds a sequence of archives.
 */
template <typename... Archives>
struct archive_sequence {};

/**
 * The built in archives.
 */
using builtin_archives = archive_sequence<
    memory_view_input_archive,
    lazy_vector_memory_output_archive
>;

/**
 * Makes a meta pair of type and id.
 */
template <typename Type, id_type id>
struct make_type;

/**
 * Registers user defined polymorphic types to serialization registry.
 */
template <typename... ExtraTypes>
class register_types;

/**
 * A no operation class, registers an empty list of types.
 */
template <>
class register_types<>
{
};

/**
 * Registers user defined polymorphic types to serialization registry.
 */
template <typename Type, id_type id, typename... ExtraTypes>
class register_types<make_type<Type, id>, ExtraTypes...> : private register_types<ExtraTypes...>
{
public:
    /**
     * Registers the type to the built in archives of the serializer.
     */
    register_types() noexcept :
        register_types(builtin_archives())
    {
    }

    /**
     * Registers the type to every archive in the given archive sequence.
     */
    template <typename... Archives>
    register_types(archive_sequence<Archives...> archives) noexcept
    {
        register_type_to_archives(archives);
    }

private:
    /**
     * Registers the type to every archive in the given archive sequence.
     */
    template <typename Archive, typename... Archives>
    void register_type_to_archives(archive_sequence<Archive, Archives...>) noexcept
    {
        // Register the type to the first archive.
        register_type_to_archive<Archive>();

        // Register the type to the other archives.
        register_type_to_archives(archive_sequence<Archives...>());
    }

    /**
     * Registers the type to an empty archive sequence - does nothing.
     */
    void register_type_to_archives(archive_sequence<>) noexcept
    {
    }

    /**
     * Registers the type to the given archive.
     * Must throw no exceptions since this will most likely execute
     * during static construction.
     * The effect of failure is that the type will not be registered,
     * it will be detected during runtime.
     */
    template <typename Archive>
    void register_type_to_archive() noexcept
    {
        try {
            registry<Archive>::get_instance().template add<Type, id>();
        } catch (...) {
        }
    }
}; // register_types

/**
 * Accepts a name and returns its serialization id.
 * We return the first 8 bytes of the sha1 on the given name.
 */
template <std::size_t size>
constexpr id_type make_id(const char (&name)[size])
{
    // Initialize constants.
    std::uint32_t h0 = 0x67452301u;
    std::uint32_t h1 = 0xEFCDAB89u;
    std::uint32_t h2 = 0x98BADCFEu;
    std::uint32_t h3 = 0x10325476u;
    std::uint32_t h4 = 0xC3D2E1F0u;

    // Initialize the message size in bits.
    std::uint64_t message_size = (size - 1) * 8;

    // Calculate the size aligned to 64 bytes (512 bits).
    constexpr std::size_t aligned_message_size = (((size + sizeof(std::uint64_t)) + 63) / 64) * 64;
        
    // Construct the pre-processed message.
    std::uint32_t preprocessed_message[aligned_message_size / sizeof(std::uint32_t)] = {};
    for (std::size_t i = 0; i < size - 1; ++i) {
        preprocessed_message[i / 4] |= detail::swap_byte_order(std::uint32_t(name[i]) 
            << ((sizeof(std::uint32_t) - 1 - (i % 4)) * 8));
    }

    // Append the byte 0x80.
    preprocessed_message[(size - 1) / 4] |= detail::swap_byte_order(std::uint32_t(0x80) 
        << ((sizeof(std::uint32_t) - 1 - ((size - 1) % 4)) * 8));

    // Append the length in bits, in 64 bit, big endian.
    preprocessed_message[(aligned_message_size / sizeof(std::uint32_t)) - 2] = 
        detail::swap_byte_order(std::uint32_t(message_size >> 32));
    preprocessed_message[(aligned_message_size / sizeof(std::uint32_t)) - 1] = 
        detail::swap_byte_order(std::uint32_t(message_size));

    // Process the message in successive 512-bit chunks.
    for (std::size_t i = 0; i < (aligned_message_size / sizeof(std::uint32_t)); i += 16) {
        std::uint32_t w[80] = {};

        // Set the value of w.
        for (std::size_t j = 0; j < 16; ++j) {
            w[j] = preprocessed_message[i + j];
        }

        // Extend the sixteen 32-bit words into eighty 32-bit words.
        for (std::size_t j = 16; j < 80; ++j) {
            w[j] = detail::swap_byte_order(detail::rotate_left(detail::swap_byte_order(
                w[j - 3] ^ w[j - 8] ^
                   w[j - 14] ^ w[j - 16]), 1));
        }

        // Initialize hash values for this chunk.
        auto a = h0;
        auto b = h1;
        auto c = h2;
        auto d = h3;
        auto e = h4;

        // Main loop.
        for (std::size_t j = 0; j < 80; ++j) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (j <= 19) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (j <= 39) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (j <= 59) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }

            auto temp = detail::rotate_left(a, 5) + f + e + k + 
                detail::swap_byte_order(w[j]);
            e = d;
            d = c;
            c = detail::rotate_left(b, 30);
            b = a;
            a = temp;
        }

        // Add this chunk's hash to result so far.
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    // Produce the first 8 bytes of the hash in little endian.
    return detail::swap_byte_order((std::uint64_t(h0) << 32) | h1);
} // make_id

} // serializer
} // zpp

#endif
