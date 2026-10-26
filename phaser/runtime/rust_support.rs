#![allow(
    non_camel_case_types,
    non_snake_case,
    non_upper_case_globals,
    dead_code
)]

use std::marker::PhantomData;

#[repr(C)]
struct PhaserBytes {
    data: *mut u8,
    size: usize,
}

#[repr(C)]
struct RawAnyMessage {
    _private: [u8; 0],
}

extern "C" {
    fn phaser_free(p: *mut u8);
    fn phaser_AnyMessage_free(msg: *mut RawAnyMessage);
    fn phaser_AnyMessage_clear(msg: *mut RawAnyMessage);
    fn phaser_AnyMessage_set_type_url(msg: *mut RawAnyMessage, data: *const u8, len: usize);
    fn phaser_AnyMessage_type_url(msg: *const RawAnyMessage) -> PhaserBytes;
    fn phaser_AnyMessage_has_type_url(msg: *const RawAnyMessage) -> u8;
    fn phaser_AnyMessage_clear_type_url(msg: *mut RawAnyMessage);
    fn phaser_AnyMessage_set_value(msg: *mut RawAnyMessage, data: *const u8, len: usize);
    fn phaser_AnyMessage_value(msg: *const RawAnyMessage) -> PhaserBytes;
    fn phaser_AnyMessage_has_value(msg: *const RawAnyMessage) -> u8;
    fn phaser_AnyMessage_clear_value(msg: *mut RawAnyMessage);
    fn phaser_AnyMessage_serialize_as_string(msg: *const RawAnyMessage) -> PhaserBytes;
    fn phaser_AnyMessage_parse_from_string(
        msg: *mut RawAnyMessage,
        data: *const u8,
        len: usize,
    ) -> u8;
    fn phaser_AnyMessage_byte_size_long(msg: *const RawAnyMessage) -> usize;
}

fn flag(value: u8) -> bool {
    value != 0
}

fn rust_ptr<T>(ptr: *mut T) -> *mut T {
    assert!(!ptr.is_null(), "phaser C call failed");
    ptr
}

fn phaser_bytes_ptr(bytes: &[u8]) -> *const u8 {
    if bytes.is_empty() {
        std::ptr::null()
    } else {
        bytes.as_ptr()
    }
}

fn take_bytes(buf: PhaserBytes) -> Vec<u8> {
    let out = if buf.data.is_null() || buf.size == 0 {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(buf.data, buf.size) }.to_vec()
    };
    if !buf.data.is_null() {
        unsafe { phaser_free(buf.data) }
    }
    out
}

fn copy_bytes(buf: PhaserBytes) -> Vec<u8> {
    take_bytes(buf)
}

fn copy_string(buf: PhaserBytes) -> String {
    let bytes = take_bytes(buf);
    match String::from_utf8(bytes) {
        Ok(text) => text,
        Err(err) => String::from_utf8_lossy(err.as_bytes()).into_owned(),
    }
}

pub struct AnyMessage<'a> {
    ptr: *mut RawAnyMessage,
    _parent: PhantomData<&'a ()>,
}

impl<'a> Drop for AnyMessage<'a> {
    fn drop(&mut self) {
        if !self.ptr.is_null() {
            unsafe { phaser_AnyMessage_free(self.ptr) }
            self.ptr = std::ptr::null_mut();
        }
    }
}

impl<'a> AnyMessage<'a> {
    fn from_raw(ptr: *mut RawAnyMessage) -> AnyMessage<'a> {
        AnyMessage {
            ptr: rust_ptr(ptr),
            _parent: PhantomData,
        }
    }

    pub fn clear(&mut self) {
        unsafe { phaser_AnyMessage_clear(self.ptr) }
    }

    pub fn set_type_url(&mut self, value: &str) {
        let bytes = value.as_bytes();
        unsafe { phaser_AnyMessage_set_type_url(self.ptr, phaser_bytes_ptr(bytes), bytes.len()) }
    }

    pub fn type_url(&self) -> String {
        copy_string(unsafe { phaser_AnyMessage_type_url(self.ptr) })
    }

    pub fn has_type_url(&self) -> bool {
        flag(unsafe { phaser_AnyMessage_has_type_url(self.ptr) })
    }

    pub fn clear_type_url(&mut self) {
        unsafe { phaser_AnyMessage_clear_type_url(self.ptr) }
    }

    pub fn set_value(&mut self, value: &[u8]) {
        unsafe { phaser_AnyMessage_set_value(self.ptr, phaser_bytes_ptr(value), value.len()) }
    }

    pub fn value(&self) -> Vec<u8> {
        copy_bytes(unsafe { phaser_AnyMessage_value(self.ptr) })
    }

    pub fn has_value(&self) -> bool {
        flag(unsafe { phaser_AnyMessage_has_value(self.ptr) })
    }

    pub fn clear_value(&mut self) {
        unsafe { phaser_AnyMessage_clear_value(self.ptr) }
    }

    pub fn serialize_as_string(&self) -> Vec<u8> {
        copy_bytes(unsafe { phaser_AnyMessage_serialize_as_string(self.ptr) })
    }

    pub fn parse_from_string(&mut self, bytes: &[u8]) -> bool {
        flag(unsafe {
            phaser_AnyMessage_parse_from_string(self.ptr, phaser_bytes_ptr(bytes), bytes.len())
        })
    }

    pub fn byte_size_long(&self) -> usize {
        unsafe { phaser_AnyMessage_byte_size_long(self.ptr) }
    }
}
