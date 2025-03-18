/*
 *
 * Copyright 2015 gRPC authors.
 *
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
 *
 */

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include "absl/flags/parse.h"
#include "helper.h"
#ifdef BAZEL_BUILD
#include "examples/protos/route_guide.grpc.pb.h"
#else
#include "route_guide.grpc.pb.h"
#endif

#include <google/protobuf/empty.pb.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;
using routeguide::Feature;
using routeguide::Point;
using routeguide::Rectangle;
using routeguide::RouteGuide;
using routeguide::RouteNote;
using routeguide::RouteSummary;
using google::protobuf::Empty;

using namespace std::chrono;

#define PINGPONG_COUNT 500000

class Timer {
  std::string name_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_;

 public:
  explicit Timer(const std::string& name)
      : name_(name), start_(std::chrono::high_resolution_clock::now()) {}
  ~Timer() {
    auto end = std::chrono::high_resolution_clock::now();
    auto duration =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start_)
            .count();
    std::string strOutput =
        name_ + " took " + std::to_string(duration) + " ms\n";

    std::cout << name_ + " took " + std::to_string(duration) + " ms" << std::endl;
    //OutputDebugStringA(strOutput.c_str());
  }
};

Point MakePoint(long latitude, long longitude) {
  Point p;
  p.set_latitude(latitude);
  p.set_longitude(longitude);
  return p;
}

Feature MakeFeature(const std::string& name, long latitude, long longitude) {
  Feature f;
  f.set_name(name);
  f.mutable_location()->CopyFrom(MakePoint(latitude, longitude));
  return f;
}

RouteNote MakeRouteNote(const std::string& message, long latitude,
                        long longitude) {
  RouteNote n;
  n.set_message(message);
  n.mutable_location()->CopyFrom(MakePoint(latitude, longitude));
  return n;
}

class RouteGuideClient {
 public:
  RouteGuideClient(std::shared_ptr<Channel> channel, const std::string& db)
      : stub_(RouteGuide::NewStub(channel)) {
    routeguide::ParseDb(db, &feature_list_);
  }

  void GetFeature() {
    Point point;
    Feature feature;
    point = MakePoint(409146138, -746188906);

    Timer timer(__func__);

    for (int i = 0; i < PINGPONG_COUNT; i++) {
      GetOneFeature(point, &feature);
    }
#if 0
    point = MakePoint(0, 0);
    GetOneFeature(point, &feature);
#endif
  }

  void ListFeatures() {
    routeguide::Rectangle rect;
    Feature feature;
    ClientContext context;

    rect.mutable_lo()->set_latitude(400000000);
    rect.mutable_lo()->set_longitude(-750000000);
    rect.mutable_hi()->set_latitude(420000000);
    rect.mutable_hi()->set_longitude(-730000000);
    std::cout << "Looking for features between 40, -75 and 42, -73"
              << std::endl;

    std::unique_ptr<ClientReader<Feature> > reader(
        stub_->ListFeatures(&context, rect));
    while (reader->Read(&feature)) {
      std::cout << "Found feature called " << feature.name() << " at "
                << feature.location().latitude() / kCoordFactor_ << ", "
                << feature.location().longitude() / kCoordFactor_ << std::endl;
    }
    Status status = reader->Finish();
    if (status.ok()) {
      std::cout << "ListFeatures rpc succeeded." << std::endl;
    } else {
      std::cout << "ListFeatures rpc failed." << std::endl;
    }
  }

  void RecordRoute() {
    Point point;
    RouteSummary stats;
    ClientContext context;
    const int kPoints = 10;
    unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();

    std::default_random_engine generator(seed);
    std::uniform_int_distribution<int> feature_distribution(
        0, feature_list_.size() - 1);
    std::uniform_int_distribution<int> delay_distribution(500, 1500);

    std::unique_ptr<ClientWriter<Point> > writer(
        stub_->RecordRoute(&context, &stats));
    for (int i = 0; i < kPoints; i++) {
      const Feature& f = feature_list_[feature_distribution(generator)];
      std::cout << "Visiting point " << f.location().latitude() / kCoordFactor_
                << ", " << f.location().longitude() / kCoordFactor_
                << std::endl;
      if (!writer->Write(f.location())) {
        // Broken stream.
        break;
      }
      std::this_thread::sleep_for(
          std::chrono::milliseconds(delay_distribution(generator)));
    }
    writer->WritesDone();
    Status status = writer->Finish();
    if (status.ok()) {
      std::cout << "Finished trip with " << stats.point_count() << " points\n"
                << "Passed " << stats.feature_count() << " features\n"
                << "Travelled " << stats.distance() << " meters\n"
                << "It took " << stats.elapsed_time() << " seconds"
                << std::endl;
    } else {
      std::cout << "RecordRoute rpc failed." << std::endl;
    }
  }

  void RouteChat() {
    ClientContext context;

    Empty response;
    std::shared_ptr<ClientWriter<RouteNote> > writer(
        stub_->RouteChat(&context, &response));

#if 0
    std::vector<uint64_t> send_timestamps;
    std::vector<uint64_t> recv_timestamps;
#endif

    Timer timer(__func__);

    for (int i = 0; i < PINGPONG_COUNT; i++) {
      RouteNote note;
      std::string strSequence = "Message " + std::to_string(i);

#if 0
      auto send_time = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count();
      send_timestamps.push_back(send_time);
#endif

      //stream->Write(note, grpc::WriteOptions().clear_buffer_hint());
      writer->Write(note);
    }
    writer->WritesDone();

    //std::cout << "Client Streaming writing ends at " << duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count() << std::endl;

#if 0
    RouteNote server_note;
    while (stream->Read(&server_note)) {
      //auto recv_time = duration_cast<microseconds>(high_resolution_clock::now().time_since_epoch()).count();
      //recv_timestamps.push_back(recv_time);
    }
#endif

    Status status = writer->Finish();
    if (!status.ok()) {
      std::cout << "RouteChat rpc failed." << std::endl;
    }

#if 0
    for (size_t i = 0; i < send_timestamps.size(); ++i) {
      std::cout << "Send[" << i << "]: " << send_timestamps[i]
                << " us, Receive[" << i << "]: "
                << (i < recv_timestamps.size() ? recv_timestamps[i] : 0)
                << " us" << std::endl;
    }
#endif
  }

 private:
  bool GetOneFeature(const Point& point, Feature* feature) {
    ClientContext context;
    Status status = stub_->GetFeature(&context, point, feature);
    if (!status.ok()) {
      std::cout << "GetFeature rpc failed." << std::endl;
      return false;
    }
#if 0
    if (!feature->has_location()) {
      std::cout << "Server returns incomplete feature." << std::endl;
      return false;
    }
    if (feature->name().empty()) {
      std::cout << "Found no feature at "
                << feature->location().latitude() / kCoordFactor_ << ", "
                << feature->location().longitude() / kCoordFactor_ << std::endl;
    }
    else {
      std::cout << "Found feature called " << feature->name() << " at "
                << feature->location().latitude() / kCoordFactor_ << ", "
                << feature->location().longitude() / kCoordFactor_ << std::endl;
    }
#endif
    return true;
  }

  const float kCoordFactor_ = 10000000.0;
  std::unique_ptr<RouteGuide::Stub> stub_;
  std::vector<Feature> feature_list_;
};

int main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);
  // Expect only arg: --db_path=path/to/route_guide_db.json.
  std::string db = routeguide::GetDbFileContent(argc, argv);

#if 0
  int desired_size = 1;
  grpc::ChannelArguments channel_args;
  channel_args.SetInt(GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES, desired_size);

  int write_buf_size = 1;
  channel_args.SetInt(GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE, write_buf_size);

  grpc_channel_args args = channel_args.c_channel_args();
  for (size_t i = 0; i < args.num_args; i++) {
    if (strcmp(args.args[i].key, GRPC_ARG_HTTP2_STREAM_LOOKAHEAD_BYTES) == 0) {
      desired_size = args.args[i].value.integer;
      continue;
    }

    if (strcmp(args.args[i].key, GRPC_ARG_HTTP2_WRITE_BUFFER_SIZE) == 0) {
      write_buf_size = args.args[i].value.integer;
      continue;
    }
  }

  RouteGuideClient guide(
      grpc::CreateCustomChannel("localhost:50051", grpc::InsecureChannelCredentials(), channel_args),
      db);
#endif

  RouteGuideClient guide(
      grpc::CreateChannel("localhost:50051",
                          grpc::InsecureChannelCredentials()),
      db);

  std::cout << "-------------- GetFeature (Unary Sync Mode) --------------" << std::endl;
  guide.GetFeature();
  std::cout << "-------------- GetFeature Done! --------------" << std::endl;

#if 0
  std::cout << "-------------- ListFeatures --------------" << std::endl;
  guide.ListFeatures();
  std::cout << "-------------- RecordRoute --------------" << std::endl;
  guide.RecordRoute();
#endif
  std::cout << "-------------- RouteChat (Streaming Mode) --------------" << std::endl;
  guide.RouteChat();
  std::cout << "-------------- RouteChat Done! --------------" << std::endl;

  return 0;
}
