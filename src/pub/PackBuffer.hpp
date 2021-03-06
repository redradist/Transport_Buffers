/**
 * @file PackBuffer.hpp
 * @author Denis Kotov
 * @date 17 Apr 2017
 * @brief Contains abstract class for Pack Buffer
 * @copyright MIT License. Open source: https://github.com/redradist/PUB.git
 */

#ifndef BUFFERS_PACKBUFFER_HPP
#define BUFFERS_PACKBUFFER_HPP

#include <stdint.h>
#include <cstring>
#include <string>
#include <vector>
#include <list>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <type_traits>

#include "AlignMemory.hpp"

namespace buffers {
  /**
   * Pack buffer class
   */
  class PackBuffer {
   public:
    /**
     * Class that is responsible for holding current PackBuffer context:
     *     next position in the message, size of left message space
     * NOTE: This class should be used only by reference in custom PackBuffer
     */
    class Context {
     public:
      friend class PackBuffer;

      Context(const Context&) = delete;
      Context(Context&&) = delete;
      Context& operator=(const Context&) = delete;
      Context& operator=(Context&&) = delete;

      Context & operator +=(const size_t & _size) {
#ifdef __cpp_exceptions
        if (buf_size_ < (msg_size_ + _size)) {
          throw std::out_of_range("Acquire more memory than is available !!");
        }
#endif

        const uint32_t kAlignedSize = getAlignedSize(_size);
        p_msg_ += kAlignedSize;
        msg_size_ += kAlignedSize;
        return *this;
      }

      Context & operator -=(const size_t & _size) {
#ifdef __cpp_exceptions
        if (msg_size_ < _size) {
          throw std::out_of_range("Release more memory than was originally !!");
        }
#endif

        const uint32_t kAlignedSize = getAlignedSize(_size);
        p_msg_ -= kAlignedSize;
        msg_size_ -= kAlignedSize;
        return *this;
      }

      uint8_t * buffer() const {
        return p_msg_;
      }

      size_t buffer_size() const {
        return (buf_size_ - msg_size_);
      }

     private:
      Context(uint8_t * _pMsg, size_t _size, AlignMemory _alignment)
          : buf_size_{_size}
          , p_msg_{_pMsg}
          , msg_size_{0}
          , alignment_{_alignment} {
      }

      uint32_t getAlignedSize(const size_t & _size) const {
        const auto kAlignedMemChunk = static_cast<uint32_t>(alignment_);
        const uint32_t kNumChunks = _size / kAlignedMemChunk + (_size % kAlignedMemChunk == 0 ? 0 : 1);
        return kNumChunks * kAlignedMemChunk;
      }

      const size_t buf_size_;
      uint8_t * p_msg_;
      size_t msg_size_;
      AlignMemory alignment_;
    };

    /**
     * Forward declaration of real delegate pack buffer
     * @tparam T Type for packing
     */
    template <typename T>
    class DelegatePackBuffer;

   public:
    /**
     * Constructor in which should be put prepared buffer.
     * DO NOT DELETE MEMORY BY YOURSELF INSIDE OF THIS CLASS !!
     * @param pMsg Pointer to the buffer
     * @param size Size of the buffer
     */
    PackBuffer(uint8_t * const _pMsg, const size_t size, AlignMemory _alignment = static_cast<AlignMemory>(sizeof(int)))
        : p_buf_(_pMsg)
        , context_(_pMsg, size, _alignment) {
    }

    /**
     * Delegate constructor for packing buffer.
     * THIS VERSION COULD BE UNSAFE IN CASE OF DUMMY USING !!
     * Better to use main constructor
     * @param _pMsg Pointer to the raw buffer
     */
    PackBuffer(uint8_t * const _pMsg, AlignMemory _alignment = static_cast<AlignMemory>(sizeof(int)))
        : PackBuffer(_pMsg, std::numeric_limits<size_t>::max(), _alignment) {
    }

    /**
     * Destructor for deletion memory if it was allocated by purselves
     */
    virtual ~PackBuffer();

   public:
    bool put(nullptr_t) = delete;

    template<typename T>
    bool put(const T & _t) {
      using GeneralType = typename std::remove_reference<
                            typename std::remove_cv<T>::type
                          >::type;
      auto packer = DelegatePackBuffer<GeneralType>{};
      bool result = packer.put(context_, _t);
      return result;
    }

    template <typename T, size_t dataLen>
    bool put(const T (&_buffer)[dataLen]) {
      auto packer = DelegatePackBuffer<T>{};
      bool result = packer.put(context_, _buffer);
      return result;
    }

    template <size_t dataLen>
    bool put(const char (&_buffer)[dataLen]);

    template<typename T>
    bool put(const T * _buffer, size_t dataLen) {
      auto packer = DelegatePackBuffer<T>{};
      bool result = packer.put(context_, _buffer, dataLen);
      return result;
    }

    template< typename T >
    static size_t getTypeSize() {
      return DelegatePackBuffer<T>{}.getTypeSize();
    }

    template< typename T >
    static size_t getTypeSize(const T & _t) {
      return DelegatePackBuffer<T>{}.getTypeSize(_t);
    }

    template< typename T, size_t dataLen >
    static size_t getTypeSize(const T (&_array)[dataLen]) {
      return DelegatePackBuffer<T>{}.getTypeSize(_array);
    }

    template< typename T >
    static size_t getTypeSize(const T * _t, size_t dataLen) {
      return DelegatePackBuffer<T>{}.getTypeSize(_t, dataLen);
    }

    /**
     * Method for reset packing data to the buffer
     */
    void reset() {
      context_ -= context_.msg_size_;
    }

    /**
     * Method implicit conversion buffer to the raw pointer
     * @return Raw pointer to the packed data
     */
    operator uint8_t const *() const {
      return p_buf_;
    }

    /**
     * Method for getting raw pointer to packed buffer
     * @return Raw pointer to the packed data
     */
    uint8_t const * getData() const {
      return p_buf_;
    }

    /**
     * Method for getting size of raw pointer to packed buffer
     * @return Size of raw pointer to packed buffer
     */
    size_t getDataSize() const {
      return context_.msg_size_;
    }

    /**
     * Method for getting size of packed buffer
     * @return Size of packed buffer
     */
    size_t getBufferSize() const {
      return context_.buffer_size();
    }

   protected:
    uint8_t * const p_buf_;
    Context context_;
  };

  inline
  PackBuffer::~PackBuffer() {
  }

  /**
   * Class which PackBuffer delegate real unpacking of data for trivial type
   * @tparam T Data to unpack. Should be a trivial type
   */
  template <typename T>
  class PackBuffer::DelegatePackBuffer {
#if __cplusplus > 199711L
    static_assert(std::is_trivial<T>::value, "Type T is not a trivial type !!");
#endif

   public:
    /**
     * Method for packing in buffer constant or temporary data
     * @tparam T Type of packing data
     * @param t Data for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const T & t) {
      bool result = false;
      if (getTypeSize() <= _ctx.buffer_size()) {
        const uint8_t *p_start_ = reinterpret_cast<const uint8_t *>(&t);
        std::copy(p_start_, p_start_ + sizeof(T), _ctx.buffer());
        _ctx += sizeof(T);
        result = true;
      }
      return result;
    }

    /**
     * Method for packing in buffer array of data
     * @tparam dataLen Array lenght
     * @param _buffer Array to packing data
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext, size_t dataLen>
    static bool put(TBufferContext & _ctx, const T (&_buffer)[dataLen]) {
      bool result = false;
      if (getTypeSize(_buffer) <= _ctx.size()) {
        DelegatePackBuffer<decltype(dataLen)>{}.put(_ctx, dataLen);
        const uint8_t *p_start_ = reinterpret_cast<const uint8_t *>(_buffer);
        std::copy(p_start_, p_start_ + sizeof(T) * dataLen, _ctx.data());
        _ctx += sizeof(T) * dataLen;
        result = true;
      }
      return result;
    }

    /**
     * Method for packing in buffer array of data
     * @tparam T Type of packing data
     * @param _buffer Pointer on first element of packing data
     * @param _dataLen Length of data to be stored
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const T * _buffer, const size_t _dataLen) {
      bool result = false;
      if (_buffer && getTypeSize(_buffer, _dataLen) <= _ctx.buffer_size()) {
        DelegatePackBuffer<decltype(_dataLen)>{}.put(_ctx, _dataLen);
        const uint8_t *p_start_ = reinterpret_cast<const uint8_t *>(_buffer);
        std::copy(p_start_, p_start_ + sizeof(T) * _dataLen, _ctx.buffer());
        _ctx += sizeof(T) * _dataLen;
        result = true;
      }
      return result;
    }

    static size_t getTypeSize() {
      return sizeof(T);
    }

    static size_t getTypeSize(const T &_) {
      return sizeof(T);
    }

    template< size_t dataLen >
    static size_t getTypeSize(const T (&_buffer)[dataLen]) {
      return sizeof(_buffer);
    }

    static size_t getTypeSize(const T * _buffer, const size_t dataLen) {
      return (sizeof(size_t) + sizeof(T) * dataLen);
    }
  };

  /**
   * Specialization DelegatePackBuffer class for char*
   */
  template <>
  class PackBuffer::DelegatePackBuffer<char*> {
   public:
    /**
     * Specialization for const null-terminated string
     * @param str Null-terminated string
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const char *str) {
      bool result = false;
      if (str) {
        int kCStringLen = getTypeSize(str);
        if (kCStringLen <= _ctx.buffer_size()) {
          const uint8_t *p_start_ = reinterpret_cast<const uint8_t *>(str);
          std::copy(p_start_, p_start_ + kCStringLen, _ctx.buffer());
          _ctx += kCStringLen;
          result = true;
        }
      }
      return result;
    }

    static size_t getTypeSize(const char *str) {
      return (std::strlen(str) + 1);
    }
  };

  template <size_t dataLen>
  bool PackBuffer::put(const char (&_buffer)[dataLen]) {
    auto packer = DelegatePackBuffer<char *>{};
    bool result = packer.put(context_,
                             static_cast<const char *>(_buffer));
    return result;
  }

  /**
   * Specialization DelegatePackBuffer class for std::string
   */
  template <>
  class PackBuffer::DelegatePackBuffer<std::string> {
   public:
    /**
     * Method for packing in buffer constant or temporary standard string
     * @param _str String for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const std::string & _str) {
      bool result = false;
      const int kCStringLen = getTypeSize(_str);
      if (kCStringLen <= _ctx.buffer_size()) {
        const uint8_t *p_start_ = reinterpret_cast<const uint8_t *>(_str.c_str());
        std::copy(p_start_, p_start_ + kCStringLen, _ctx.buffer());
        _ctx += kCStringLen;
        result = true;
      }
      return result;
    }

    static size_t getTypeSize(const std::string & _str) {
      return (_str.size() + 1);
    }
  };

  /**
   * Specialization DelegatePackBuffer class for std::vector
   * @tparam T Type of data under std::vector
   */
  template <typename T>
  class PackBuffer::DelegatePackBuffer<std::vector<T>> {
   public:
    /**
     * Method for packing std::vector in buffer
     * @tparam T Type of std::vector
     * @param vec std::vector for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const std::vector<T> & _vec) {
      bool result = false;
      if (_vec.size() > 0) {
        if (getTypeSize(_vec) <= _ctx.buffer_size()) {
          DelegatePackBuffer<decltype(_vec.size())>{}.put(_ctx, _vec.size());
          std::copy(_vec.data(), _vec.data() + _vec.size(), (T*)(_ctx.buffer()));
          _ctx += _vec.size() * sizeof(T);
          result = true;
        }
      }
      return result;
    }

    template <typename TT>
    static typename std::enable_if<(std::is_trivial<TT>::value), size_t>::type
    getTypeSize(const std::vector<TT> & _vec) {
      return (sizeof(_vec.size()) + sizeof(TT) * _vec.size());
    }


    template <typename TT>
    static typename std::enable_if<!(std::is_trivial<TT>::value), size_t>::type
    getTypeSize(const std::vector<TT> & _vec) {
      size_t typeSize = sizeof(_vec.size());
      for (auto& ve : _vec) {
        typeSize += DelegatePackBuffer<TT>{}.getTypeSize(ve);
      }
      return typeSize;
    }
  };

  /**
   * Specialization DelegatePackBuffer class for std::list
   * @tparam T Type of data under std::list
   */
  template <typename T>
  class PackBuffer::DelegatePackBuffer<std::list<T>> {
   public:
    /**
     * Method for packing std::list in buffer
     * @tparam T Type of std::list
     * @param _lst std::list for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const std::list<T> & _lst) {
      bool result = false;
      if (_lst.size() > 0) {
        if (getTypeSize(_lst) <= _ctx.buffer_size()) {
          DelegatePackBuffer<decltype(_lst.size())>{}.put(_ctx, _lst.size());
          for (auto ve : _lst) {
            DelegatePackBuffer<T>{}.put(_ctx, ve);
          }
          result = true;
        }
      }
      return result;
    }

    template <typename TT>
    static typename std::enable_if<(std::is_trivial<TT>::value), size_t>::type
    getTypeSize(const std::list<TT> & _lst) {
      return (sizeof(_lst.size()) + sizeof(TT) * _lst.size());
    }

    template <typename TT>
    static typename std::enable_if<!(std::is_trivial<TT>::value), size_t>::type
    getTypeSize(const std::list<TT> & _lst) {
      size_t typeSize = sizeof(_lst.size());
      for (auto& ve : _lst) {
        typeSize += DelegatePackBuffer<TT>{}.getTypeSize(ve);
      }
      return typeSize;
    }
  };

  /**
   * Specialization DelegatePackBuffer class for std::set
   * @tparam K Type of data under std::set
   */
  template <typename K>
  class PackBuffer::DelegatePackBuffer<std::set<K>> {
   public:
    /**
     * Method for packing std::set in buffer
     * @tparam K Type of std::set
     * @param mp std::set for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const std::set<K> & _set) {
      bool result = false;
      if (_set.size() > 0) {
        if (getTypeSize(_set) <= _ctx.buffer_size()) {
          DelegatePackBuffer<decltype(_set.size())>{}.put(_ctx, _set.size());
          for (auto& ve : _set) {
            DelegatePackBuffer<K>{}.put(_ctx, ve);
          }
          result = true;
        }
      }
      return result;
    }

    template <typename KK>
    static typename std::enable_if<(std::is_trivial<KK>::value), size_t>::type
    getTypeSize(const std::set<KK> & _mp) {
      return (sizeof(_mp.size()) + sizeof(KK) * _mp.size());
    }

    template <typename KK>
    static typename std::enable_if<!(std::is_trivial<KK>::value), size_t>::type
    getTypeSize(const std::set<KK> & _set) {
      size_t typeSize = sizeof(_set.size());
      for (auto& ve : _set) {
        typeSize += DelegatePackBuffer<KK>{}.getTypeSize(ve);
      }
      return typeSize;
    }
  };

  /**
   * Specialization DelegatePackBuffer class for std::pair
   * @tparam K First value of std::pair
   * @tparam V Second value of std::pair
   */
  template <typename K, typename V>
  class PackBuffer::DelegatePackBuffer<std::pair<K, V>> {
   public:
    /**
     * Method for packing std::map in buffer
     * @tparam K Key of std::map
     * @tparam V Value of std::map
     * @param mp std::map for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const std::pair<K, V> & _pr) {
      bool result = false;
      if (getTypeSize(_pr) <= _ctx.buffer_size()) {
        DelegatePackBuffer<K>{}.put(_ctx, _pr.first);
        DelegatePackBuffer<V>{}.put(_ctx, _pr.second);
        result = true;
      }
      return result;
    }

    static size_t getTypeSize(const std::pair<K, V> & _pr) {
      return (DelegatePackBuffer<K>{}.getTypeSize(_pr.first) + DelegatePackBuffer<V>{}.getTypeSize(_pr.second));
    }
  };

  /**
   * Specialization DelegatePackBuffer class for std::map
   * @tparam K Key of std::map
   * @tparam V Value of std::map
   */
  template <typename K, typename V>
  class PackBuffer::DelegatePackBuffer<std::map<K, V>> {
   public:
    /**
     * Method for packing std::map in buffer
     * @tparam K Key of std::map
     * @tparam V Value of std::map
     * @param _mp std::map for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const std::map<K, V> & _mp) {
      bool result = false;
      if (_mp.size() > 0) {
        if (getTypeSize(_mp) <= _ctx.buffer_size()) {
          DelegatePackBuffer<decltype(_mp.size())>{}.put(_ctx, _mp.size());
          for (auto& ve : _mp) {
            DelegatePackBuffer<K>{}.put(_ctx, ve.first);
            DelegatePackBuffer<V>{}.put(_ctx, ve.second);
          }
          result = true;
        }
      }
      return result;
    }

    template <typename KK, typename VV>
    static typename std::enable_if<(std::is_trivial<KK>::value && std::is_trivial<VV>::value), size_t>::type
    getTypeSize(const std::unordered_map<KK, VV> & _mp) {
      return (sizeof(_mp.size()) + (sizeof(KK) + sizeof(VV)) * _mp.size());
    }

    template <typename KK, typename VV>
    static typename std::enable_if<!(std::is_trivial<KK>::value && std::is_trivial<VV>::value), size_t>::type
    getTypeSize(const std::map<KK, VV> & _mp) {
      size_t typeSize = sizeof(_mp.size());
      for (auto& ve : _mp) {
        typeSize += DelegatePackBuffer<KK>{}.getTypeSize(ve.first);
        typeSize += DelegatePackBuffer<VV>{}.getTypeSize(ve.second);
      }
      return typeSize;
    }
  };

  /**
   * Specialization DelegatePackBuffer class for std::unordered_set
   * @tparam K Type of data under std::unordered_set
   */
  template <typename K>
  class PackBuffer::DelegatePackBuffer<std::unordered_set<K>> {
   public:
    /**
     * Method for packing std::unordered_set in buffer
     * @tparam K Type of std::unordered_set
     * @param mp std::unordered_set for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const std::unordered_set<K> & _set) {
      bool result = false;
      if (_set.size() > 0) {
        if (getTypeSize(_set) <= _ctx.buffer_size()) {
          DelegatePackBuffer<decltype(_set.size())>{}.put(_ctx, _set.size());
          for (auto& ve : _set) {
            DelegatePackBuffer<K>{}.put(_ctx, ve);
          }
          result = true;
        }
      }
      return result;
    }

    template <typename KK>
    static typename std::enable_if<(std::is_trivial<KK>::value), size_t>::type
    getTypeSize(const std::unordered_set<KK> & _mp) {
      return (sizeof(_mp.size()) + sizeof(KK) * _mp.size());
    }

    template <typename KK>
    static typename std::enable_if<!(std::is_trivial<KK>::value), size_t>::type
    getTypeSize(const std::unordered_set<KK> & _set) {
      size_t typeSize = sizeof(_set.size());
      for (auto& ve : _set) {
        typeSize += DelegatePackBuffer<KK>{}.getTypeSize(ve);
      }
      return typeSize;
    }
  };

  /**
   * Specialization DelegatePackBuffer class for std::unordered_map
   * @tparam K Key of std::unordered_map
   * @tparam V Value of std::unordered_map
   */
  template <typename K, typename V>
  class PackBuffer::DelegatePackBuffer<std::unordered_map<K, V>> {
   public:
    /**
     * Method for packing std::unordered_map in buffer
     * @tparam K Key of std::unordered_map
     * @tparam V Value of std::unordered_map
     * @param mp std::unordered_map for packing
     * @return Return true if packing is succeed, false otherwise
     */
    template <typename TBufferContext>
    static bool put(TBufferContext & _ctx, const std::unordered_map<K, V> & _mp) {
      bool result = false;
      if (_mp.size() > 0) {
        if (getTypeSize(_mp) <= _ctx.buffer_size()) {
          DelegatePackBuffer<decltype(_mp.size())>{}.put(_ctx, _mp.size());
          for (auto& ve : _mp) {
            DelegatePackBuffer<K>{}.put(_ctx, ve.first);
            DelegatePackBuffer<V>{}.put(_ctx, ve.second);
          }
          result = true;
        }
      }
      return result;
    }

    template <typename KK, typename VV>
    static typename std::enable_if<(std::is_trivial<KK>::value && std::is_trivial<VV>::value), size_t>::type
    getTypeSize(const std::unordered_map<KK, VV> & _mp) {
      return (sizeof(_mp.size()) + (sizeof(KK) + sizeof(VV)) * _mp.size());
    }

    template <typename KK, typename VV>
    static typename std::enable_if<!(std::is_trivial<KK>::value && std::is_trivial<VV>::value), size_t>::type
    getTypeSize(const std::unordered_map<KK, VV> & _mp) {
      size_t typeSize = sizeof(_mp.size());
      for (auto& ve : _mp) {
        typeSize += DelegatePackBuffer<KK>{}.getTypeSize(ve.first);
        typeSize += DelegatePackBuffer<VV>{}.getTypeSize(ve.second);
      }
      return typeSize;
    }
  };

  template <typename T>
  PackBuffer& operator<<(PackBuffer& buffer, T && t) {
    buffer.put(std::forward<T>(t));
    return buffer;
  }
}

#endif //BUFFERS_PACKBUFFER_HPP