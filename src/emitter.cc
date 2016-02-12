
#include <iostream>
#include <stdint.h>
#include <v8.h>
#include <nan.h>
#include <uv.h>
#include <cstring>
#include "conversion.h"
#include "bacaddr.h"
#include "rp.h"
#include "emitter.h"
#include "listenable.h"
#include "functions.h"

using namespace v8;

static Nan::Persistent<Object> eventEmitter;

struct IamEvent {
  uv_work_t  request;
  uint32_t device_id;
  unsigned max_apdu;
  int segmentation;
  uint16_t vendor_id;
  BACNET_ADDRESS * src;
};

// called by libuv worker in separate thread
static void DoNothing(uv_work_t *req) {
}


Local<Object> iamToJ(Nan::HandleScope *scope, IamEvent *work) {
    Local<Object> iamEvent = Nan::New<Object>();
    Nan::Set(iamEvent, Nan::New("deviceId").ToLocalChecked(), Nan::New(work->device_id));
    Nan::Set(iamEvent, Nan::New("vendorId").ToLocalChecked(), Nan::New(work->vendor_id));
    Nan::Set(iamEvent, Nan::New("segmentation").ToLocalChecked(), Nan::New(work->segmentation));
    Nan::Set(iamEvent, Nan::New("src").ToLocalChecked(), bacnetAddressToJ(scope, work->src));
    return iamEvent;
}

// called by libuv in event loop when async function completes
static void IamEmitAsyncComplete(uv_work_t *req,int status) {
    Nan::HandleScope scope;
    IamEvent *work = static_cast<IamEvent *>(req->data);

    Local<Object> iamEvent = iamToJ(&scope, work);
    Local<Value> argv[] = {
        Nan::New("iam").ToLocalChecked(),
        iamEvent
    };

    Local<Object> localEventEmitter = Nan::New(eventEmitter);
    Nan::MakeCallback(localEventEmitter, "emit", 2, argv);

    delete work;
}

void emit_iam(uint32_t device_id, unsigned max_apdu, int segmentation, uint16_t vendor_id, BACNET_ADDRESS * src) {
    IamEvent * event = new IamEvent();
    event->request.data = event;
    event->device_id = device_id;
    event->max_apdu = max_apdu;
    event->segmentation = segmentation;
    event->vendor_id = vendor_id;
    event->src = src;

    // kick off the worker thread
    uv_queue_work(uv_default_loop(), &event->request,DoNothing,IamEmitAsyncComplete);
}

struct RPAEvent {
    uint8_t invoke_id;
    uv_work_t  request;
    BACNET_READ_PROPERTY_DATA data;
};

// called by libuv in event loop when async function completes
static void ReadPropertyAckEmitAsyncComplete(uv_work_t *req,int status) {
    RPAEvent *work = static_cast<RPAEvent *>(req->data);

    Nan::HandleScope scope;
    Local<Object> localEventEmitter = Nan::New(eventEmitter);

    Local<Object> property = readPropertyAckToJ(&scope, &work->data);

    // emit the read property ack in case you want all of those
    Local<Value> emit_rp_a_args[] = {
            Nan::New("read-property-ack").ToLocalChecked(),
            property,
            Nan::New(work->invoke_id),
        };
    Nan::MakeCallback(localEventEmitter, "emit", 3, emit_rp_a_args);

    // emit the general ack - it is used for firing callbacks of by invoke_id
    Local<Value> emit_a_args[] = {
            Nan::New("ack").ToLocalChecked(),
            Nan::New(work->invoke_id),
            property,
        };
    Nan::MakeCallback(localEventEmitter, "emit", 3, emit_a_args);

    delete work->data.application_data;
    delete work;
}

void emit_read_property_ack(uint8_t invoke_id, BACNET_READ_PROPERTY_DATA * data) {
    uint8_t * application_data = new uint8_t[data->application_data_len];
    memcpy(application_data, data->application_data, data->application_data_len);
    RPAEvent * event = new RPAEvent();
    event->invoke_id = invoke_id;
    event->request.data = event;
    event->data = *data;
    event->data.application_data = application_data;

    // kick off the worker thread
    uv_queue_work(uv_default_loop(), &event->request, DoNothing, ReadPropertyAckEmitAsyncComplete);
}

struct AbortEvent {
    BACNET_ADDRESS src;
    uint8_t invoke_id;
    uint8_t abort_reason;
    uv_work_t  request;
};

// called by libuv in event loop when async function completes
static void AbortEmitAsyncComplete(uv_work_t *req, int status) {
    AbortEvent *work = static_cast<AbortEvent *>(req->data);

    Nan::HandleScope scope;
    Local<Object> localEventEmitter = Nan::New(eventEmitter);

    // emit the abort - it is used for firing callbacks by invoke_id
    Local<Value> emit_a_args[] = {
            Nan::New("abort").ToLocalChecked(),
            Nan::New(work->invoke_id),
            abortReasonToJ(&scope, work->abort_reason)
        };
    Nan::MakeCallback(localEventEmitter, "emit", 3, emit_a_args);

    delete work;
}

void emit_abort(
       BACNET_ADDRESS * src,
       uint8_t invoke_id,
       uint8_t abort_reason,
       bool server) {
    AbortEvent * event = new AbortEvent();
    event->invoke_id = invoke_id;
    event->src = *src;
    event->abort_reason = abort_reason;
    event->request.data = event;

    // kick off the worker thread
    uv_queue_work(uv_default_loop(), &event->request, DoNothing, AbortEmitAsyncComplete);
}

void eventEmitterSet(Local<Object> localEventEmitter) {
    Nan::HandleScope scope;
    eventEmitter.Reset(localEventEmitter);
}