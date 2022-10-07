// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

#pragma once

#include "context.h"
#include "randomgen.h"
#include "valcheck.h"
#include "utils/common.h"
#include "utils/defines.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace troy
{
    /**
    Class to store a ciphertext element. The data for a ciphertext consists
    of two or more polynomials, which are in Microsoft SEAL stored in a CRT
    form with respect to the factors of the coefficient modulus. This data
    itself is not meant to be modified directly by the user, but is instead
    operated on by functions in the Evaluator class. The size of the backing
    array of a ciphertext depends on the encryption parameters and the size
    of the ciphertext (at least 2). If the size of the ciphertext is T,
    the poly_modulus_degree encryption parameter is N, and the number of
    primes in the coeff_modulus encryption parameter is K, then the
    ciphertext backing array requires precisely 8*N*K*T bytes of memory.
    A ciphertext also carries with it the parms_id of its associated
    encryption parameters, which is used to check the validity of the
    ciphertext for homomorphic operations and decryption.

    @par Memory Management
    The size of a ciphertext refers to the number of polynomials it contains,
    whereas its capacity refers to the number of polynomials that fit in the
    current memory allocation. In high-performance applications unnecessary
    re-allocations should be avoided by reserving enough memory for the
    ciphertext to begin with either by providing the desired capacity to the
    constructor as an extra argument, or by calling the reserve function at
    any time.

    @par Thread Safety
    In general, reading from ciphertext is thread-safe as long as no other
    thread is concurrently mutating it. This is due to the underlying data
    structure storing the ciphertext not being thread-safe.

    @see Plaintext for the class that stores plaintexts.
    */
    class Ciphertext
    {
    public:
        using ct_coeff_type = std::uint64_t;

        /**
        Constructs an empty ciphertext allocating no memory.

        @param[in] pool The MemoryPoolHandle pointing to a valid memory pool
        @throws std::invalid_argument if pool is uninitialized
        */
        Ciphertext() : data_()
        {}

        /**
        Constructs an empty ciphertext with capacity 2. In addition to the
        capacity, the allocation size is determined by the highest-level
        parameters associated to the given SEALContext.

        @param[in] context The SEALContext
        @param[in] pool The MemoryPoolHandle pointing to a valid memory pool
        @throws std::invalid_argument if the encryption parameters are not valid
        @throws std::invalid_argument if pool is uninitialized
        */
        explicit Ciphertext(const SEALContext &context)
            : data_()
        {
            // Allocate memory but don't resize
            reserve(context, 2);
        }

        /**
        Constructs an empty ciphertext with capacity 2. In addition to the
        capacity, the allocation size is determined by the encryption parameters
        with given parms_id.

        @param[in] context The SEALContext
        @param[in] parms_id The parms_id corresponding to the encryption
        parameters to be used
        @param[in] pool The MemoryPoolHandle pointing to a valid memory pool
        @throws std::invalid_argument if the encryption parameters are not valid
        @throws std::invalid_argument if parms_id is not valid for the encryption
        parameters
        @throws std::invalid_argument if pool is uninitialized
        */
        explicit Ciphertext(
            const SEALContext &context, ParmsID parms_id)
            : data_()
        {
            // Allocate memory but don't resize
            reserve(context, parms_id, 2);
        }

        /**
        Constructs an empty ciphertext with given capacity. In addition to
        the capacity, the allocation size is determined by the given
        encryption parameters.

        @param[in] context The SEALContext
        @param[in] parms_id The parms_id corresponding to the encryption
        parameters to be used
        @param[in] size_capacity The capacity
        @param[in] pool The MemoryPoolHandle pointing to a valid memory pool
        @throws std::invalid_argument if the encryption parameters are not valid
        @throws std::invalid_argument if parms_id is not valid for the encryption
        parameters
        @throws std::invalid_argument if size_capacity is less than 2 or too large
        @throws std::invalid_argument if pool is uninitialized
        */
        explicit Ciphertext(
            const SEALContext &context, ParmsID parms_id, std::size_t size_capacity)
            : data_()
        {
            // Allocate memory but don't resize
            reserve(context, parms_id, size_capacity);
        }

        /**
        Creates a new ciphertext by copying a given one.

        @param[in] copy The ciphertext to copy from
        */
        Ciphertext(const Ciphertext &copy) = default;

        /**
        Creates a new ciphertext by moving a given one.

        @param[in] source The ciphertext to move from
        */
        Ciphertext(Ciphertext &&source) = default;

        /**
        Allocates enough memory to accommodate the backing array of a ciphertext
        with given capacity. In addition to the capacity, the allocation size is
        determined by the encryption parameters corresponing to the given
        parms_id.

        @param[in] context The SEALContext
        @param[in] parms_id The parms_id corresponding to the encryption
        parameters to be used
        @param[in] size_capacity The capacity
        @throws std::invalid_argument if the encryption parameters are not valid
        @throws std::invalid_argument if parms_id is not valid for the encryption
        parameters
        @throws std::invalid_argument if size_capacity is less than 2 or too large
        */
        void reserve(const SEALContext &context, ParmsID parms_id, std::size_t size_capacity);

        /**
        Allocates enough memory to accommodate the backing array of a ciphertext
        with given capacity. In addition to the capacity, the allocation size is
        determined by the highest-level parameters associated to the given
        SEALContext.

        @param[in] context The SEALContext
        @param[in] size_capacity The capacity
        @throws std::invalid_argument if the encryption parameters are not valid
        @throws std::invalid_argument if size_capacity is less than 2 or too large
        */
        inline void reserve(const SEALContext &context, std::size_t size_capacity)
        {
            auto parms_id = context.firstParmsID();
            reserve(context, parms_id, size_capacity);
        }

        /**
        Allocates enough memory to accommodate the backing array of a ciphertext
        with given capacity. In addition to the capacity, the allocation size is
        determined by the current encryption parameters.

        @param[in] size_capacity The capacity
        @throws std::invalid_argument if size_capacity is less than 2 or too large
        @throws std::logic_error if the encryption parameters are not
        */
        inline void reserve(std::size_t size_capacity)
        {
            // Note: poly_modulus_degree_ and coeff_modulus_size_ are either valid
            // or coeff_modulus_size_ is zero (in which case no memory is allocated).
            reserveInternal(size_capacity, poly_modulus_degree_, coeff_modulus_size_);
        }

        /**
        Resizes the ciphertext to given size, reallocating if the capacity
        of the ciphertext is too small. The ciphertext parameters are
        determined by the given SEALContext and parms_id.

        This function is mainly intended for internal use and is called
        automatically by functions such as Evaluator::multiply and
        Evaluator::relinearize. A normal user should never have a reason
        to manually resize a ciphertext.

        @param[in] context The SEALContext
        @param[in] parms_id The parms_id corresponding to the encryption
        parameters to be used
        @param[in] size The new size
        @throws std::invalid_argument if the encryption parameters are not valid
        @throws std::invalid_argument if parms_id is not valid for the encryption
        parameters
        @throws std::invalid_argument if size is less than 2 or too large
        */
        void resize(const SEALContext &context, ParmsID parms_id, std::size_t size);

        /**
        Resizes the ciphertext to given size, reallocating if the capacity
        of the ciphertext is too small. The ciphertext parameters are
        determined by the highest-level parameters associated to the given
        SEALContext.

        This function is mainly intended for internal use and is called
        automatically by functions such as Evaluator::multiply and
        Evaluator::relinearize. A normal user should never have a reason
        to manually resize a ciphertext.

        @param[in] context The SEALContext
        @param[in] size The new size
        @throws std::invalid_argument if the encryption parameters are not valid
        @throws std::invalid_argument if size is less than 2 or too large
        */
        inline void resize(const SEALContext &context, std::size_t size)
        {
            auto parms_id = context.firstParmsID();
            resize(context, parms_id, size);
        }

        /**
        Resizes the ciphertext to given size, reallocating if the capacity
        of the ciphertext is too small.

        This function is mainly intended for internal use and is called
        automatically by functions such as Evaluator::multiply and
        Evaluator::relinearize. A normal user should never have a reason
        to manually resize a ciphertext.

        @param[in] size The new size
        @throws std::invalid_argument if size is less than 2 or too large
        */
        inline void resize(std::size_t size)
        {
            // Note: poly_modulus_degree_ and coeff_modulus_size_ are either valid
            // or coeff_modulus_size_ is zero (in which case no memory is allocated).
            resizeInternal(size, poly_modulus_degree_, coeff_modulus_size_);
        }

        /**
        Resets the ciphertext. This function releases any memory allocated
        by the ciphertext, returning it to the memory pool. It also sets all
        encryption parameter specific size information to zero.
        */
        inline void release() noexcept
        {
            parms_id_ = parmsIDZero;
            is_ntt_form_ = false;
            size_ = 0;
            poly_modulus_degree_ = 0;
            coeff_modulus_size_ = 0;
            scale_ = 1.0;
            correction_factor_ = 1;
            data_.release();
        }

        /**
        Copies a given ciphertext to the current one.

        @param[in] assign The ciphertext to copy from
        */
        Ciphertext &operator=(const Ciphertext &assign);

        /**
        Moves a given ciphertext to the current one.

        @param[in] assign The ciphertext to move from
        */
        Ciphertext &operator=(Ciphertext &&assign) = default;

        /**
        Returns a reference to the backing DynArray object.
        */
        inline const auto &dynArray() const noexcept
        {
            return data_;
        }

        /**
        Returns a pointer to the beginning of the ciphertext data.
        */
        inline ct_coeff_type *data() noexcept
        {
            return data_.begin();
        }

        /**
        Returns a const pointer to the beginning of the ciphertext data.
        */
        inline const ct_coeff_type *data() const noexcept
        {
            return data_.cbegin();
        }

        /**
        Returns a pointer to a particular polynomial in the ciphertext
        data. Note that Microsoft SEAL stores each polynomial in the ciphertext
        modulo all of the K primes in the coefficient modulus. The pointer
        returned by this function is to the beginning (constant coefficient)
        of the first one of these K polynomials.

        @param[in] poly_index The index of the polynomial in the ciphertext
        @throws std::out_of_range if poly_index is less than 0 or bigger
        than the size of the ciphertext
        */
        inline ct_coeff_type *data(std::size_t poly_index)
        {
            auto poly_uint64_count = util::mul_safe(poly_modulus_degree_, coeff_modulus_size_);
            if (poly_uint64_count == 0)
            {
                return nullptr;
            }
            if (poly_index >= size_)
            {
                throw std::out_of_range("poly_index must be within [0, size)");
            }
            return data_.begin() + util::safe_cast<std::size_t>(util::mul_safe(poly_index, poly_uint64_count));
        }

        /**
        Returns a const pointer to a particular polynomial in the
        ciphertext data. Note that Microsoft SEAL stores each polynomial in the
        ciphertext modulo all of the K primes in the coefficient modulus.
        The pointer returned by this function is to the beginning
        (constant coefficient) of the first one of these K polynomials.

        @param[in] poly_index The index of the polynomial in the ciphertext
        @throws std::out_of_range if poly_index is out of range
        */
        inline const ct_coeff_type *data(std::size_t poly_index) const
        {
            auto poly_uint64_count = util::mul_safe(poly_modulus_degree_, coeff_modulus_size_);
            if (poly_uint64_count == 0)
            {
                return nullptr;
            }
            if (poly_index >= size_)
            {
                throw std::out_of_range("poly_index must be within [0, size)");
            }
            return data_.cbegin() + util::safe_cast<std::size_t>(util::mul_safe(poly_index, poly_uint64_count));
        }

        /**
        Returns a reference to a polynomial coefficient at a particular
        index in the ciphertext data. If the polynomial modulus has degree N,
        and the number of primes in the coefficient modulus is K, then the
        ciphertext contains size*N*K coefficients. Thus, the coeff_index has
        a range of [0, size*N*K).

        @param[in] coeff_index The index of the coefficient
        @throws std::out_of_range if coeff_index is out of range
        */
        inline ct_coeff_type &operator[](std::size_t coeff_index)
        {
            return data_.at(coeff_index);
        }

        /**
        Returns a const reference to a polynomial coefficient at a particular
        index in the ciphertext data. If the polynomial modulus has degree N,
        and the number of primes in the coefficient modulus is K, then the
        ciphertext contains size*N*K coefficients. Thus, the coeff_index has
        a range of [0, size*N*K).

        @param[in] coeff_index The index of the coefficient
        @throws std::out_of_range if coeff_index is out of range
        */
        inline const ct_coeff_type &operator[](std::size_t coeff_index) const
        {
            return data_.at(coeff_index);
        }

        /**
        Returns the number of primes in the coefficient modulus of the
        associated encryption parameters. This directly affects the
        allocation size of the ciphertext.
        */
        inline std::size_t coeffModulusSize() const noexcept
        {
            return coeff_modulus_size_;
        }

        /**
        Returns the degree of the polynomial modulus of the associated
        encryption parameters. This directly affects the allocation size
        of the ciphertext.
        */
        inline std::size_t polyModulusDegree() const noexcept
        {
            return poly_modulus_degree_;
        }

        /**
        Returns the size of the ciphertext.
        */
        inline std::size_t size() const noexcept
        {
            return size_;
        }

        /**
        Returns the capacity of the allocation. This means the largest size
        of the ciphertext that can be stored in the current allocation with
        the current encryption parameters.
        */
        inline std::size_t sizeCapacity() const noexcept
        {
            std::size_t poly_uint64_count = poly_modulus_degree_ * coeff_modulus_size_;
            return poly_uint64_count ? data_.capacity() / poly_uint64_count : std::size_t(0);
        }

        /**
        Check whether the current ciphertext is transparent, i.e. does not require
        a secret key to decrypt. In typical security models such transparent
        ciphertexts would not be considered to be valid. Starting from the second
        polynomial in the current ciphertext, this function returns true if all
        following coefficients are identically zero. Otherwise, returns false.
        */
        inline bool isTransparent() const
        {
            return (
                !data_.size() || (size_ < SEAL_CIPHERTEXT_SIZE_MIN) ||
                std::all_of(data(1), data_.cend(), util::isZero<ct_coeff_type>));
        }

        // /**
        // Returns an upper bound on the size of the ciphertext, as if it was written
        // to an output stream.

        // @param[in] compr_mode The compression mode
        // @throws std::invalid_argument if the compression mode is not supported
        // @throws std::logic_error if the size does not fit in the return type
        // */
        // std::streamoff save_size(compr_mode_type compr_mode = Serialization::compr_mode_default) const;

        // /**
        // Saves the ciphertext to an output stream. The output is in binary format
        // and not human-readable. The output stream must have the "binary" flag set.

        // @param[out] stream The stream to save the ciphertext to
        // @param[in] compr_mode The desired compression mode
        // @throws std::invalid_argument if the compression mode is not supported
        // @throws std::logic_error if the data to be saved is invalid, or if
        // compression failed
        // @throws std::runtime_error if I/O operations failed
        // */
        // inline std::streamoff save(
        //     std::ostream &stream, compr_mode_type compr_mode = Serialization::compr_mode_default) const
        // {
        //     using namespace std::placeholders;
        //     return Serialization::Save(
        //         std::bind(&Ciphertext::save_members, this, _1), save_size(compr_mode_type::none), stream, compr_mode,
        //         false);
        // }

        // /**
        // Loads a ciphertext from an input stream overwriting the current ciphertext.
        // No checking of the validity of the ciphertext data against encryption
        // parameters is performed. This function should not be used unless the
        // ciphertext comes from a fully trusted source.

        // @param[in] context The SEALContext
        // @param[in] stream The stream to load the ciphertext from
        // @throws std::invalid_argument if the encryption parameters are not valid
        // @throws std::logic_error if the data cannot be loaded by this version of
        // Microsoft SEAL, if the loaded data is invalid, or if decompression failed
        // @throws std::runtime_error if I/O operations failed
        // */
        // inline std::streamoff unsafe_load(const SEALContext &context, std::istream &stream)
        // {
        //     using namespace std::placeholders;
        //     return Serialization::Load(std::bind(&Ciphertext::load_members, this, context, _1, _2), stream, false);
        // }

        // /**
        // Loads a ciphertext from an input stream overwriting the current ciphertext.
        // The loaded ciphertext is verified to be valid for the given SEALContext.

        // @param[in] context The SEALContext
        // @param[in] stream The stream to load the ciphertext from
        // @throws std::invalid_argument if the encryption parameters are not valid
        // @throws std::logic_error if the data cannot be loaded by this version of
        // Microsoft SEAL, if the loaded data is invalid, or if decompression failed
        // @throws std::runtime_error if I/O operations failed
        // */
        // inline std::streamoff load(const SEALContext &context, std::istream &stream)
        // {
        //     Ciphertext new_data(pool());
        //     auto in_size = new_data.unsafe_load(context, stream);
        //     if (!is_valid_for(new_data, context))
        //     {
        //         throw std::logic_error("ciphertext data is invalid");
        //     }
        //     std::swap(*this, new_data);
        //     return in_size;
        // }

        // /**
        // Saves the ciphertext to a given memory location. The output is in binary
        // format and is not human-readable.

        // @param[out] out The memory location to write the ciphertext to
        // @param[in] size The number of bytes available in the given memory location
        // @param[in] compr_mode The desired compression mode
        // @throws std::invalid_argument if out is null or if size is too small to
        // contain a SEALHeader, or if the compression mode is not supported
        // @throws std::logic_error if the data to be saved is invalid, or if
        // compression failed
        // @throws std::runtime_error if I/O operations failed
        // */
        // inline std::streamoff save(
        //     seal_byte *out, std::size_t size, compr_mode_type compr_mode = Serialization::compr_mode_default) const
        // {
        //     using namespace std::placeholders;
        //     return Serialization::Save(
        //         std::bind(&Ciphertext::save_members, this, _1), save_size(compr_mode_type::none), out, size, compr_mode,
        //         false);
        // }

        // /**
        // Loads a ciphertext from a given memory location overwriting the current
        // ciphertext. No checking of the validity of the ciphertext data against
        // encryption parameters is performed. This function should not be used
        // unless the ciphertext comes from a fully trusted source.

        // @param[in] context The SEALContext
        // @param[in] in The memory location to load the ciphertext from
        // @param[in] size The number of bytes available in the given memory location
        // @throws std::invalid_argument if the encryption parameters are not valid
        // @throws std::invalid_argument if in is null or if size is too small to
        // contain a SEALHeader
        // @throws std::logic_error if the data cannot be loaded by this version of
        // Microsoft SEAL, if the loaded data is invalid, or if decompression failed
        // @throws std::runtime_error if I/O operations failed
        // */
        // inline std::streamoff unsafe_load(const SEALContext &context, const seal_byte *in, std::size_t size)
        // {
        //     using namespace std::placeholders;
        //     return Serialization::Load(std::bind(&Ciphertext::load_members, this, context, _1, _2), in, size, false);
        // }

        // /**
        // Loads a ciphertext from a given memory location overwriting the current
        // ciphertext. The loaded ciphertext is verified to be valid for the given
        // SEALContext.

        // @param[in] context The SEALContext
        // @param[in] in The memory location to load the ciphertext from
        // @param[in] size The number of bytes available in the given memory location
        // @throws std::invalid_argument if the encryption parameters are not valid
        // @throws std::invalid_argument if in is null or if size is too small to
        // contain a SEALHeader
        // @throws std::logic_error if the data cannot be loaded by this version of
        // Microsoft SEAL, if the loaded data is invalid, or if decompression failed
        // @throws std::runtime_error if I/O operations failed
        // */
        // inline std::streamoff load(const SEALContext &context, const seal_byte *in, std::size_t size)
        // {
        //     Ciphertext new_data(pool());
        //     auto in_size = new_data.unsafe_load(context, in, size);
        //     if (!is_valid_for(new_data, context))
        //     {
        //         throw std::logic_error("ciphertext data is invalid");
        //     }
        //     std::swap(*this, new_data);
        //     return in_size;
        // }

        /**
        Returns whether the ciphertext is in NTT form.
        */
        inline bool isNttForm() const noexcept
        {
            return is_ntt_form_;
        }

        /**
        Returns whether the ciphertext is in NTT form.
        */
        inline bool &isNttForm() noexcept
        {
            return is_ntt_form_;
        }

        /**
        Returns a reference to parms_id.

        @see EncryptionParameters for more information about parms_id.
        */
        inline ParmsID &parmsID() noexcept
        {
            return parms_id_;
        }

        /**
        Returns a const reference to parms_id.

        @see EncryptionParameters for more information about parms_id.
        */
        inline const ParmsID &parmsID() const noexcept
        {
            return parms_id_;
        }

        /**
        Returns a reference to the scale. This is only needed when using the CKKS encryption scheme. The user should
        have little or no reason to ever change the scale by hand.
        */
        inline double &scale() noexcept
        {
            return scale_;
        }

        /**
        Returns a constant reference to the scale. This is only needed when using the CKKS encryption scheme.
        */
        inline const double &scale() const noexcept
        {
            return scale_;
        }

        /**
        Returns a reference to the correction factor. This is only needed when using the BGV encryption scheme. The user
        should have little or no reason to ever change the scale by hand.
        */
        inline std::uint64_t &correctionFactor() noexcept
        {
            return correction_factor_;
        }

        /**
        Returns a constant reference to the correction factor. This is only needed when using the BGV encryption scheme.
        */
        inline const std::uint64_t &correctionFactor() const noexcept
        {
            return correction_factor_;
        }

        /**
        Enables access to private members of seal::Ciphertext for SEAL_C.
        */
        struct CiphertextPrivateHelper;

    private:
        void reserveInternal(
            std::size_t size_capacity, std::size_t poly_modulus_degree, std::size_t coeff_modulus_size);

        void resizeInternal(std::size_t size, std::size_t poly_modulus_degree, std::size_t coeff_modulus_size);

        void expandSeed(const SEALContext &context, const UniformRandomGeneratorInfo &prng_info);

        // void save_members(std::ostream &stream) const;

        // void load_members(const SEALContext &context, std::istream &stream, SEALVersion version);

        inline bool hasSeedMarker() const noexcept
        {
            return (data_.size() && (size_ == 2)) ? (data(1)[0] == 0xFFFFFFFFFFFFFFFFULL) : false;
        }

        ParmsID parms_id_ = parmsIDZero;

        bool is_ntt_form_ = false;

        std::size_t size_ = 0;

        std::size_t poly_modulus_degree_ = 0;

        std::size_t coeff_modulus_size_ = 0;

        double scale_ = 1.0;

        std::uint64_t correction_factor_ = 1;

        util::HostDynamicArray<ct_coeff_type> data_;
    };
} // namespace seal
