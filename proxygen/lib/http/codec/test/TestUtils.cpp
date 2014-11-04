/*
 *  Copyright (c) 2014, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "proxygen/lib/http/codec/test/TestUtils.h"

#include <folly/Random.h>
#include <folly/io/Cursor.h>

using namespace folly::io;
using namespace folly;
using namespace std;
using namespace testing;

namespace proxygen {

const HTTPSettings kDefaultIngressSettings{
  {SettingsId::INITIAL_WINDOW_SIZE, 65536}
};

std::unique_ptr<HTTPMessage> getPriorityMessage(uint8_t priority) {
  auto ret = folly::make_unique<HTTPMessage>();
  ret->setSPDY(2);
  ret->setPriority(priority);
  return ret;
}

std::unique_ptr<folly::IOBuf> makeBuf(uint32_t size) {
  auto out = folly::IOBuf::create(size);
  out->append(size);
  // fill with random junk
  RWPrivateCursor cursor(out.get());
  while (cursor.length() >= 8) {
    cursor.write<uint64_t>(folly::Random::rand64());
  }
  while (cursor.length()) {
    cursor.write<uint8_t>((uint8_t)folly::Random::rand32());
  }
  return out;
}

std::unique_ptr<testing::NiceMock<MockHTTPCodec>>
makeMockParallelCodec(TransportDirection dir) {
  auto codec = folly::make_unique<testing::NiceMock<MockHTTPCodec>>();
  EXPECT_CALL(*codec, supportsParallelRequests())
    .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*codec, isReusable())
    .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*codec, getTransportDirection())
    .WillRepeatedly(testing::Return(dir));
  EXPECT_CALL(*codec, getIngressSettings())
    .WillRepeatedly(testing::Return(&kDefaultIngressSettings));
  return codec;
}

std::unique_ptr<testing::NiceMock<MockHTTPCodec>>
makeDownstreamParallelCodec() {
  return makeMockParallelCodec(TransportDirection::DOWNSTREAM);
}

std::unique_ptr<testing::NiceMock<MockHTTPCodec>>
makeUpstreamParallelCodec() {
  return makeMockParallelCodec(TransportDirection::UPSTREAM);
}

HTTPMessage getGetRequest(const std::string& url) {
  HTTPMessage req;
  req.setMethod("GET");
  req.setURL(url);
  req.setHTTPVersion(1, 1);
  req.getHeaders().set(HTTP_HEADER_HOST, "www.foo.com");
  return req;
}

std::unique_ptr<HTTPMessage> makeGetRequest() {
  return folly::make_unique<HTTPMessage>(getGetRequest());
}

HTTPMessage getPostRequest() {
  HTTPMessage req;
  req.setMethod("POST");
  req.setURL<string>("/");
  req.setHTTPVersion(1, 1);
  req.getHeaders().set(HTTP_HEADER_HOST, "www.foo.com");
  req.getHeaders().set(HTTP_HEADER_CONTENT_LENGTH, "200");
  return req;
}

std::unique_ptr<HTTPMessage> makePostRequest() {
  return folly::make_unique<HTTPMessage>(getPostRequest());
}

std::unique_ptr<HTTPMessage> makeResponse(uint16_t statusCode) {
  auto resp = folly::make_unique<HTTPMessage>();
  resp->setStatusCode(statusCode);
  return resp;
}

std::tuple<std::unique_ptr<HTTPMessage>, std::unique_ptr<folly::IOBuf> >
makeResponse(uint16_t statusCode, size_t len) {
  auto resp = makeResponse(statusCode);
  resp->getHeaders().set(HTTP_HEADER_CONTENT_LENGTH, folly::to<string>(len));
  return std::make_pair(std::move(resp), makeBuf(len));
}

void fakeMockCodec(MockHTTPCodec& codec) {
  // For each generate* function, write some data to the chain
  EXPECT_CALL(codec, generateHeader(_, _, _, _, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               HTTPCodec::StreamID stream,
                               const HTTPMessage& msg,
                               HTTPCodec::StreamID assocStream,
                               HTTPHeaderSize* size) {
                             writeBuf.append(makeBuf(10));
                           }));

  EXPECT_CALL(codec, generateBody(_, _, _, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               HTTPCodec::StreamID stream,
                               std::shared_ptr<folly::IOBuf> chain,
                               bool eom) {
                             auto len = chain->computeChainDataLength();
                             writeBuf.append(chain->clone());
                             return len;
                           }));

  EXPECT_CALL(codec, generateChunkHeader(_, _, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               HTTPCodec::StreamID stream,
                               size_t length) {
                             writeBuf.append(makeBuf(length));
                             return length;
                           }));

  EXPECT_CALL(codec, generateChunkTerminator(_, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               HTTPCodec::StreamID stream) {
                             writeBuf.append(makeBuf(4));
                             return 4;
                           }));

  EXPECT_CALL(codec, generateTrailers(_, _, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               HTTPCodec::StreamID stream,
                               const HTTPHeaders& trailers) {
                             writeBuf.append(makeBuf(30));
                             return 30;
                           }));

  EXPECT_CALL(codec, generateEOM(_, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               HTTPCodec::StreamID stream) {
                             writeBuf.append(makeBuf(6));
                             return 6;
                           }));

  EXPECT_CALL(codec, generateRstStream(_, _, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               HTTPCodec::StreamID stream,
                               ErrorCode code) {
                             writeBuf.append(makeBuf(6));
                             return 6;
                           }));

  EXPECT_CALL(codec, generateGoaway(_, _, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               uint32_t lastStream,
                               ErrorCode code) {
                             writeBuf.append(makeBuf(6));
                             return 6;
                           }));

  EXPECT_CALL(codec, generatePingRequest(_))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf) {
                             writeBuf.append(makeBuf(6));
                             return 6;
                           }));

  EXPECT_CALL(codec, generatePingReply(_, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               uint64_t id) {
                             writeBuf.append(makeBuf(6));
                             return 6;
                           }));

  EXPECT_CALL(codec, generateSettings(_))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf) {
                             writeBuf.append(makeBuf(6));
                             return 6;
                           }));

  EXPECT_CALL(codec, generateWindowUpdate(_, _, _))
    .WillRepeatedly(Invoke([] (folly::IOBufQueue& writeBuf,
                               HTTPCodec::StreamID stream,
                               uint32_t delta) {
                             writeBuf.append(makeBuf(6));
                             return 6;
                           }));

}

}
