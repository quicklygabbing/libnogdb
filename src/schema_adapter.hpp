/*
 *  Copyright (C) 2018, Throughwave (Thailand) Co., Ltd.
 *  <peerawich at throughwave dot co dot th>
 *
 *  This file is part of libnogdb, the NogDB core library in C++.
 *
 *  libnogdb is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __SCHEMA_ADAPTER_HPP_INCLUDED_
#define __SCHEMA_ADAPTER_HPP_INCLUDED_

#include <algorithm>
#include <vector>
#include <string>
#include <sstream>
#include <utility>
#include <iomanip>
#include <cstdlib>

#include "storage_adapter.hpp"
#include "constant.hpp"
#include "dbinfo_adapter.hpp"

namespace nogdb {

    namespace adapter {

        namespace schema {

            /**
             * Raw record format in lmdb data storage:
             * {name<string>} -> {id<uint16>}{superClassId<uint16>}{type<char>}
             */
            struct ClassAccessInfo {
                std::string name{""};
                ClassId id{0};
                ClassId superClassId{0};
                ClassType type{ClassType::UNDEFINED};
            };

            class ClassAccess : public storage_engine::adapter::LMDBKeyValAccess {
            public:
                ClassAccess(const storage_engine::LMDBTxn * const txn)
                        : LMDBKeyValAccess(txn, TB_CLASSES, true, true, false, true) {}

                virtual ~ClassAccess() noexcept = default;

                void create(const ClassAccessInfo& props) {
                    auto result = get(props.name);
                    if (result.empty) {
                        createOrUpdate(props);
                    } else {
                        throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_DUPLICATE_CLASS);
                    }
                }

                void update(const ClassAccessInfo& props) {
                    auto result = get(props.name);
                    if (!result.empty) {
                        createOrUpdate(props);
                    } else {
                        throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_NOEXST_CLASS);
                    }
                }

                void remove(const std::string& className) {
                    auto result = get(className);
                    if (!result.empty) {
                        del(className);
                    } else {
                        throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_NOEXST_CLASS);
                    }
                }

                void alterClassName(const std::string& oldName, const std::string& newName) {
                    auto result = get(oldName);
                    if (!result.empty) {
                        auto newResult = get(newName);
                        if (newResult.empty) {
                            del(oldName);
                            put(newName, result.data.blob());
                        } else {
                            throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_DUPLICATE_CLASS);
                        }
                    } else {
                        throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_NOEXST_CLASS);
                    }
                }

                ClassAccessInfo getInfo(const std::string& className) const {
                    auto result = get(className);
                    if (result.empty) {
                        return ClassAccessInfo{};
                    } else {
                        return parse(className, result.data.blob());
                    }
                }

                ClassId getId(const std::string& className) const {
                    auto result = get(className);
                    if (result.empty) {
                        return ClassId{};
                    } else {
                        return parseClassId(result.data.blob());
                    }
                }

            protected:

                static ClassAccessInfo parse(const std::string& className, const Blob& blob) {
                    return ClassAccessInfo{
                            className,
                            parseClassId(blob),
                            parseSuperClassId(blob),
                            parseClassType(blob)
                    };
                }

                static ClassId parseClassId(const Blob& blob) {
                    auto classId = ClassId{};
                    blob.retrieve(&classId, 0, sizeof(classId));
                    return classId;

                }

                static ClassId parseSuperClassId(const Blob& blob) {
                    auto superClassId = ClassId{};
                    blob.retrieve(&superClassId, sizeof(ClassId), sizeof(superClassId));
                    return superClassId;
                }

                static ClassType parseClassType(const Blob& blob) {
                    auto classType = ClassType::UNDEFINED;
                    blob.retrieve(&classType, 2 * sizeof(ClassId), sizeof(ClassType));
                    return classType;
                }

            private:

                void createOrUpdate(const ClassAccessInfo& props) {
                    auto totalLength = 2 * sizeof(ClassId) + sizeof(props.type);
                    auto value = Blob(totalLength);
                    value.append(&props.id, sizeof(ClassId));
                    value.append(&props.superClassId, sizeof(ClassId));
                    value.append(&props.type, sizeof(props.type));
                    put(props.name, value);
                }

            };

            /**
             * Raw record format in lmdb data storage:
             * {classId<string>:name<string+padding>} -> {id<uint16>}{type<char>}
             */
            struct PropertyAccessInfo {
                ClassId classId{0};
                std::string name{""};
                PropertyId id{0};
                PropertyType type{PropertyType::UNDEFINED};
            };

            constexpr char KEY_SEPARATOR = ':';
            constexpr char KEY_PADDING = ' ';
            //TODO: can we improve this const creation working faster?
            const std::string KEY_SEARCH_BEGIN = (std::stringstream{} << std::setfill(KEY_PADDING) << std::setw(MAX_PROPERTY_NAME_LEN)).str();

            class PropertyAccess : public storage_engine::adapter::LMDBKeyValAccess {
            public:
                PropertyAccess(const storage_engine::LMDBTxn * const txn)
                        : LMDBKeyValAccess(txn, TB_PROPERTIES, false, true, false, false) {}

                virtual ~PropertyAccess() noexcept = default;

                void create(const PropertyAccessInfo& props) {
                    auto propertyKey = buildKey(props.classId, props.name);
                    auto result = get(propertyKey);
                    if (result.empty) {
                        createOrUpdate(props);
                    } else {
                        throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_DUPLICATE_PROPERTY);
                    }
                }

                void remove(const ClassId& classId, const std::string& propertyName) {
                    auto propertyKey = buildKey(classId, propertyName);
                    auto result = get(propertyKey);
                    if (!result.empty) {
                        del(propertyKey);
                    } else {
                        throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_NOEXST_PROPERTY);
                    }
                }

                void alterPropertyName(const ClassId& classId, const std::string& oldName, const std::string& newName) {
                    auto propertyKey = buildKey(classId, oldName);
                    auto result = get(propertyKey);
                    if (!result.empty) {
                        auto newPropertyKey = buildKey(classId, newName);
                        auto newResult = get(newPropertyKey);
                        if (newResult.empty) {
                            del(propertyKey);
                            put(newPropertyKey, result.data.blob());
                        } else {
                            throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_DUPLICATE_PROPERTY);
                        }
                    } else {
                        throw NOGDB_CONTEXT_ERROR(NOGDB_CTX_NOEXST_PROPERTY);
                    }
                }

                PropertyAccessInfo getInfo(const ClassId& classId, const std::string& propertyName) const {
                    auto propertyKey = buildKey(classId, propertyName);
                    auto result = get(propertyKey);
                    if (result.empty) {
                        return PropertyAccessInfo{};
                    } else {
                        return parse(classId, propertyName, result.data.blob());
                    }
                }

                std::vector<PropertyAccessInfo> getInfos(const ClassId& classId) const {
                    auto result = std::vector<PropertyAccessInfo>{};
                    auto cursorHandler = cursor();
                    for(auto keyValue = cursorHandler.findRange(buildSearchKeyBegin(classId));
                        !keyValue.empty();
                        keyValue = cursorHandler.getNext()) {
                        auto keyPair = splitKey(keyValue.key.data.string());
                        auto &classIdKey = keyPair.first;
                        auto &propertyNameKey = keyPair.second;
                        if (classId != classIdKey) break;
                        result.emplace_back(parse(classIdKey, propertyNameKey, keyValue.val.data.blob()));
                    }
                    return result;
                }

                PropertyId getId(const ClassId& classId, const std::string& propertyName) const {
                    auto propertyKey = buildKey(classId, propertyName);
                    auto result = get(propertyKey);
                    if (result.empty) {
                        return PropertyId{};
                    } else {
                        return parsePropertyId(result.data.blob());
                    }
                }

            protected:

                static PropertyAccessInfo parse(const ClassId& classId, const std::string& propertyName, const Blob& blob) {
                    return PropertyAccessInfo{
                            classId,
                            propertyName,
                            parsePropertyId(blob),
                            parsePropertyType(blob)
                    };
                }

                static PropertyId parsePropertyId(const Blob& blob) {
                    auto propertyId = PropertyId{};
                    blob.retrieve(&propertyId, 0, sizeof(PropertyId));
                    return propertyId;
                }

                static PropertyType parsePropertyType(const Blob& blob) {
                    auto propertyType = PropertyType::UNDEFINED;
                    blob.retrieve(&propertyType, sizeof(PropertyId), sizeof(PropertyType));
                    return propertyType;
                }

            private:

                void createOrUpdate(const PropertyAccessInfo& props) {
                    auto totalLength = sizeof(PropertyId) + sizeof(PropertyType);
                    auto value = Blob(totalLength);
                    value.append(&props.id, sizeof(PropertyId));
                    value.append(&props.type, sizeof(PropertyType));
                    put(buildKey(props.classId, props.name), value);
                }

                //TODO: can we improve this function working faster?
                std::string frontPadding(const std::string& str, size_t length) const {
                    std::stringstream ss{};
                    ss << std::setfill(KEY_PADDING) << std::setw((int)(length - str.length())) << str;
                    return ss.str();
                }

                //TODO: can we improve this function working faster?
                std::string buildKey(const ClassId& classId, const std::string& propertyName) const {
                    std::stringstream ss{};
                    ss << std::to_string(classId) << KEY_SEPARATOR << frontPadding(propertyName, MAX_PROPERTY_NAME_LEN);
                    return ss.str();
                }

                //TODO: can we improve this function working faster?
                std::string buildSearchKeyBegin(const ClassId& classId) const {
                    std::stringstream ss{};
                    ss << std::to_string(classId) << KEY_SEPARATOR << KEY_SEARCH_BEGIN;
                    return ss.str();
                }

                std::pair<ClassId, std::string> splitKey(const std::string& key) const {
                    auto splitKey = split(key, KEY_SEPARATOR[0]);
                    require(splitKey.size() == 2);
                    auto classId = ClassId{std::atoi(splitKey[0].c_str())};
                    auto propertyName = splitKey[1];
                    ltrim(propertyName);
                    return std::make_pair(classId, propertyName);
                };

                inline void ltrim(std::string &str) const {
                    str.erase(str.begin(), std::find_if(str.begin(), str.end(), [](int ch) {
                        return !std::isspace(ch);
                    }));
                }

            };

            /**
             * Raw record format in lmdb data storage:
             * {propertyId<uint16>} -> {id<uint16>}{isComposite<uint8>}{isUnique<uint8>}{classId<uint16>}
             */
            struct IndexAccessInfo {
                PropertyId propertyId{0};
                IndexId id{0};
                uint8_t isComposite{0};
                uint8_t isUnique{1};
                ClassId classId{0};
            };

            class IndexAccess : public storage_engine::adapter::LMDBKeyValAccess {
            public:
                //TODO

            private:
                //TODO

            };

            class SchemaAccess {
                //TODO
            };

        }
    }
}


#endif //__SCHEMA_ADAPTER_HPP_INCLUDED_
