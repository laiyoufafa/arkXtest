/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef EXTERN_API_H
#define EXTERN_API_H

#include <memory>
#include <map>
#include <string>
#include <set>
#include <functional>
#include <list>
#include "common_defines.h"
#include "common_utilities_hpp.h"
#include "json.hpp"

namespace OHOS::uitest {
    enum ErrCode : uint8_t {
        NO_ERROR = 0,
        /**Internal error, not expected to happen.*/
        INTERNAL_ERROR = 1,
        /**Widget that is expected to be exist lost.*/
        WIDGET_LOST = 2,
        USAGE_ERROR = 4,
    };

    /**Get the readable name of the error enum value.*/
    std::string GetErrorName(ErrCode code);

    /**API invocation error detail wrapper.*/
    class ApiCallErr {
    public:
        ErrCode code_ = NO_ERROR;
        std::string message_;

        ApiCallErr() = delete;

        explicit ApiCallErr(ErrCode ec) : ApiCallErr(ec, "") {}

        ApiCallErr(ErrCode ec, std::string_view msg)
        {
            code_ = ec;
            message_ = "[" + GetErrorName(ec) + "]:" + std::string(msg);
        }
    };

    /**Base type of types that can be serialized into json data and deserialized from json data.*/
    class Parcelable {
    public:
        /**Serialize this object (value and typeid) into json data.*/
        virtual void WriteIntoParcel(nlohmann::json &data) const = 0;

        /**Construct this object from json data.*/
        virtual void ReadFromParcel(const nlohmann::json &data) = 0;
    };

    class ExternApiBase : public Parcelable {
    public:
        // need at least one virtual method to make this class polymorphic
        virtual ~ExternApiBase() = default;

        virtual TypeId GetTypeId() const = 0;
    };

    /** Base class of all the UiTest API classes which may be used externally which must be
     * parcelable to be transferred to/from external. */
    template<TypeId kTypeId>
    class ExternApi : public ExternApiBase {
    public:
        ~ExternApi() override = default;

        TypeId GetTypeId() const override
        {
            return kTypeId;
        }
    };

    /**Prototype of function that handles ExternAPI invocation request.
     *
     * @param function the requested function id.
     * @param callerObj the caller ExternAPI object (serialized).
     * @param in the incoming serialized parameters (json-array).
     * @param out the returning serialized objects (json-array).
     * @param error the error information.
     * @return true if the request is accepted and handled by the handler, false otherwise.
     **/
    using ApiRequstHandler = bool (*)(std::string_view function, nlohmann::json &callerObj,
                                      const nlohmann::json &in, nlohmann::json &out, ApiCallErr &err);

    /**
     * Server that accepts and handles api invocation request.
     **/
    class ExternApiServer {
    public:

        /**
         * Register api invocation  handler.
         **/
        void AddHandler(ApiRequstHandler handler);

        /**
         * Remove api invocation  handler.
         **/
        void RemoveHandler(ApiRequstHandler handler);

        /**
         * Handle api invocation request.
         *
         * */
        void Call(std::string_view apiId, nlohmann::json &caller, const nlohmann::json &in,
                  nlohmann::json &out, ApiCallErr &err) const;

        /**
         * Get the singleton instance.
         * */
        static ExternApiServer &Get();

        ~ExternApiServer() {}

    private:
        ExternApiServer() = default;

        std::list<ApiRequstHandler> handlers_;
    };

    /** Function used to read out ExternAPI transact parameters, get parameter item value
     * at the given index in the json data. */
    template<typename T>
    T GetItemValueFromJson(const nlohmann::json &data, uint32_t index)
    {
        DCHECK(index >= 0 && index < data.size());
        const nlohmann::json item = data.at(index);
        const uint32_t typeId = item[KEY_DATA_TYPE];
        if constexpr(std::is_same<T, bool>::value) {
            DCHECK(typeId == TypeId::BOOL);
            bool value = item[KEY_DATA_VALUE];
            return value;
        } else if constexpr(std::is_integral<T>::value) {
            DCHECK(typeId == TypeId::INT);
            T value = item[KEY_DATA_VALUE];
            return value;
        } else if constexpr(std::is_same<T, float>::value) {
            DCHECK(typeId == TypeId::FLOAT);
            float value = item[KEY_DATA_VALUE];
            return value;
        } else if constexpr(std::is_same<T, std::string>::value) {
            DCHECK(typeId == TypeId::STRING);
            std::string value = item[KEY_DATA_VALUE];
            return value;
        } else if constexpr(std::is_same<T, nlohmann::json>::value) {
            DCHECK(typeId >= TypeId::BY && typeId <= TypeId::RECT_JSON);
            nlohmann::json value = item[KEY_DATA_VALUE];
            return value;
        } else {
            static_assert(!std::is_same<T, T>::value, "Unsupported type");
        }
    }

    /**Function used to write ExternAPI transact results, serialize return value and push into json array.*/
    template<typename T>
    void PushBackValueItemIntoJson(const T &value, nlohmann::json &out)
    {
        nlohmann::json item;
        if constexpr(std::is_same<T, bool>::value) {
            item[KEY_DATA_TYPE] = TypeId::BOOL;
            item[KEY_DATA_VALUE] = value;
        } else if constexpr(std::is_integral<T>::value) {
            item[KEY_DATA_TYPE] = TypeId::INT;
            item[KEY_DATA_VALUE] = value;
        } else if constexpr(std::is_same<T, float>::value) {
            item[KEY_DATA_TYPE] = TypeId::FLOAT;
            item[KEY_DATA_VALUE] = value;
        } else if constexpr(std::is_same<T, std::string>::value) {
            item[KEY_DATA_TYPE] = TypeId::STRING;
            item[KEY_DATA_VALUE] = value;
        } else if constexpr(std::is_base_of<ExternApiBase, T>::value) {
            item[KEY_DATA_TYPE] = value.GetTypeId();
            nlohmann::json objData;
            value.WriteIntoParcel(objData);
            item[KEY_DATA_VALUE] = objData;
        } else {
            static_assert(!std::is_same<T, T>::value, "Unsupported type");
        }
        out.push_back(item);
    }

    /** Function serving external api-transaction with json-parcel incoming arguments and outgoing results.*/
    std::string ApiTransact(std::string_view func, std::string_view caller, std::string_view params);

    /**Function to register creator and function invoker of all the <b>ExternAPI</b> types.*/
    void RegisterExternApIs();
}

#endif