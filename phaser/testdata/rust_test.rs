use test_message_c_phaser::{EnumTest, TestMessage};

#[test]
fn getters_setters_and_round_trip() {
    let mut msg = TestMessage::new(8192);
    assert!(!msg.has_x());
    msg.set_x(0);
    assert!(msg.has_x());
    assert_eq!(msg.x(), 0);
    msg.clear_x();
    assert!(!msg.has_x());

    msg.set_y(-5i64);
    msg.set_s("hi");
    msg.set_buffer(b"a\0b");
    msg.set_e(EnumTest::FOO);
    msg.set_fl(1.5f32);
    msg.set_db(2.5);
    assert_eq!(msg.y(), -5i64);
    assert_eq!(msg.s(), "hi");
    assert_eq!(msg.buffer(), b"a\0b");
    assert_eq!(msg.e(), EnumTest::FOO);
    assert!(msg.has_e());
    assert_eq!(msg.fl(), 1.5f32);
    assert_eq!(msg.db(), 2.5);

    msg.add_vi32(3);
    msg.add_vi32(4);
    msg.set_vi32(1, 9);
    assert_eq!(msg.vi32_size(), 2);
    assert_eq!(msg.vi32(0), 3);
    assert_eq!(msg.vi32(1), 9);
    msg.add_vstr("ab");
    msg.set_vstr(0, "z");
    assert_eq!(msg.vstr(0), "z");

    {
        let mut nested = msg.mutable_m();
        nested.set_str("nested");
        nested.set_f(-2i64);
        nested.add_ev(EnumTest::BAR);
    }
    assert!(msg.has_m());
    assert_eq!(msg.m().str(), "nested");
    assert_eq!(msg.m().f(), -2i64);
    assert_eq!(msg.m().ev(0), EnumTest::BAR);

    {
        let mut child = msg.add_vm();
        child.set_str("e0");
    }
    msg.mutable_vm(0).set_str("e1");
    assert_eq!(msg.vm_size(), 1);
    assert_eq!(msg.vm(0).str(), "e1");

    assert_eq!(msg.u1_case(), 0);
    msg.set_u1a(7u32);
    assert_eq!(msg.u1_case(), 107);
    assert_eq!(msg.u1a(), 7u32);
    assert!(msg.has_u1a());
    msg.set_u1b(8u64);
    assert_eq!(msg.u1_case(), 108);
    assert!(!msg.has_u1a());
    assert_eq!(msg.u1b(), 8u64);

    msg.set_u2b("zz");
    assert_eq!(msg.u2_case(), 110);
    assert_eq!(msg.u2b(), "zz");
    msg.set_u2a(5i64);
    assert_eq!(msg.u2_case(), 109);
    assert!(!msg.has_u2b());
    assert_eq!(msg.u2a(), 5i64);

    {
        let mut arm = msg.mutable_u3b();
        arm.set_str("arm");
    }
    assert_eq!(msg.u3_case(), 112);
    assert_eq!(msg.u3b().str(), "arm");

    {
        let mut entry = msg.add_values();
        entry.set_key("k");
        entry.set_value(3);
    }
    assert_eq!(msg.values_size(), 1);
    assert_eq!(msg.values(0).key(), "k");
    assert_eq!(msg.values(0).value(), 3);

    assert!(!msg.has_any());
    let type_url = {
        let mut packed = msg.mutable_any();
        packed.set_type_url("type.googleapis.com/foo.bar.InnerMessage");
        packed.set_value(&[0, 1]);
        assert_eq!(
            packed.type_url(),
            "type.googleapis.com/foo.bar.InnerMessage"
        );
        assert_eq!(packed.value(), &[0, 1]);
        packed.type_url()
    };
    assert!(msg.has_any());

    assert!(msg.byte_size_long() > 0);
    assert!(msg.debug_string().contains("nested"));
    let blob = msg.serialize_as_string();
    let mut copy = TestMessage::new(1024);
    assert!(copy.parse_from_string(&blob));
    assert_eq!(copy.s(), "hi");
    assert_eq!(copy.buffer(), b"a\0b");
    assert_eq!(copy.e(), EnumTest::FOO);
    assert_eq!(copy.m().str(), "nested");
    assert_eq!(copy.vm(0).str(), "e1");
    assert_eq!(copy.values(0).key(), "k");
    assert_eq!(copy.any().type_url(), type_url);
    assert!(copy.byte_size_long() > 0);
    copy.clear();
    assert!(!copy.has_s());
    assert_eq!(copy.vstr_size(), 0);
    assert!(!copy.has_any());
}
