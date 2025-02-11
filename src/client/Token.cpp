/********************************************************************
 * Copyright (c) 2013 - 2014, Pivotal Inc.
 * All rights reserved.
 *
 * Author: Zhanwei Wang
 ********************************************************************/
/********************************************************************
 * 2014 -
 * open source under Apache License Version 2.0
 ********************************************************************/
/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "Exception.h"
#include "ExceptionInternal.h"
#include "Hash.h"
#include "Token.h"
#include "WritableUtils.h"

#include <gsasl.h>

using namespace Hdfs::Internal;

namespace Hdfs {
namespace Internal {

static std::string Base64Encode(const char * input, size_t len) {
    int rc = 0;
    size_t outLen;
    char * output = NULL;
    std::string retval;

    if (GSASL_OK != (rc = gsasl_base64_to(input, len, &output, &outLen))) {
        assert(GSASL_MALLOC_ERROR == rc);
        throw std::bad_alloc();
    }

    assert(NULL != output);
    retval = output;
    gsasl_free(output);

    for (size_t i = 0 ; i < retval.length(); ++i) {
        switch (retval[i]) {
        case '+':
            retval[i] = '-';
            break;

        case '/':
            retval[i] = '_';
            break;

        case '=':
            retval.resize(i);
            break;

        default:
            break;
        }
    }

    return retval;
}

static void Base64Decode(const std::string & urlSafe,
                         std::vector<char> & buffer) {
    int retval = 0, append = 0;
    size_t outLen;
    char * output = NULL;
    std::string input = urlSafe;

    for (size_t i = 0; i < input.length(); ++i) {
        switch (input[i]) {
        case '-':
            input[i] = '+';
            break;

        case '_':
            input[i] = '/';
            break;

        default:
            break;
        }
    }

    while (true) {
        retval = gsasl_base64_from(input.data(), input.length(), &output, &outLen);

        if (GSASL_OK != retval) {
            switch (retval) {
            case GSASL_BASE64_ERROR:
                if (append++ < 2) {
                    input.append("=");
                    continue;
                }

                throw std::invalid_argument(
                    "invalid input of gsasl_base64_from");

            case GSASL_MALLOC_ERROR:
                throw std::bad_alloc();

            default:
                assert(
                    false
                    && "unexpected return value from gsasl_base64_from");
            }
        }

        break;
    }

    assert(outLen >= 0);
    buffer.resize(outLen);
    memcpy(buffer.data(), output, outLen);
    gsasl_free(output);
}

std::string Token::toString() const {
    try {
        size_t len = 0;
        std::vector<char> buffer(1024);
        WritableUtils out(buffer.data(), buffer.size());
        len += out.WriteInt32(identifier.size());
        len += out.WriteRaw(identifier.data(), identifier.size());
        len += out.WriteInt32(password.size());
        len += out.WriteRaw(password.data(), password.size());
        len += out.WriteText(kind);
        len += out.WriteText(service);
        return Base64Encode(buffer.data(), len);
    } catch (...) {
        NESTED_THROW(HdfsIOException, "cannot convert token to string");
    }
}

Token & Token::fromString(const std::string & str) {
    int32_t len;

    try {
        std::vector<char> buffer;
        Base64Decode(str, buffer);
        WritableUtils in(buffer.data(), buffer.size());
        len = in.ReadInt32();
        identifier.resize(len);
        in.ReadRaw(const_cast<char*>(identifier.data()), len);
        len = in.ReadInt32();
        password.resize(len);
        in.ReadRaw(const_cast<char*>(password.data()), len);
        kind = in.ReadText();
        service = in.ReadText();
        return *this;
    } catch (...) {
        NESTED_THROW(HdfsInvalidBlockToken,
                     "cannot construct a token from the string");
    }
}

size_t Token::hash_value() const {
    size_t values[] = { StringHasher(identifier), StringHasher(password),
                        StringHasher(kind), StringHasher(service)
                      };
    return CombineHasher(values, sizeof(values) / sizeof(values[0]));
}

}
}
