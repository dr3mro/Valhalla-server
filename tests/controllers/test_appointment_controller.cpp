// Tests for AppointmentController — covers Create/Read/Update/Delete/Search paths.
// AppointmentController delegates entirely to EntityController.
#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "controllers/appointmentcontroller/appointmentcontroller.hpp"
#include "entities/appointments/clinic.hpp"
#include "entities/appointments/laboratory.hpp"
#include "entities/appointments/pharmacy.hpp"
#include "entities/appointments/radiologycenter.hpp"
#include "utils/global/http.hpp"
#include "utils/global/requester.hpp"

using Requester = api::v2::Requester;

struct Result { int status = 0; std::string body; };
static Result capture(auto fn)
{
    Result r;
    fn([&](int s, const std::string& b){ r.status = s; r.body = b; });
    return r;
}

// ── ClinicAppointment ────────────────────────────────────────────────────────

TEST_CASE("AppointmentController<ClinicAppointment>::Create rejects malformed JSON", "[controllers][appointment]")
{
    AppointmentController<ClinicAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Create(std::move(cb), Requester(1, "users"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("AppointmentController<ClinicAppointment>::Read rejects malformed JSON", "[controllers][appointment]")
{
    AppointmentController<ClinicAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Read(std::move(cb), Requester(1, "users"), "{{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("AppointmentController<ClinicAppointment>::Read with missing id → 400", "[controllers][appointment]")
{
    AppointmentController<ClinicAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Read(std::move(cb), Requester(1, "users"), R"({"schema":["date"]})");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("AppointmentController<ClinicAppointment>::Update with no id → 400", "[controllers][appointment]")
{
    AppointmentController<ClinicAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Update(std::move(cb), Requester(1, "users"), R"({"date":"2026-01-01"})", std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("AppointmentController<ClinicAppointment>::Delete with no id → 406", "[controllers][appointment]")
{
    AppointmentController<ClinicAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Delete(std::move(cb), Requester(1, "users"), std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("AppointmentController<ClinicAppointment>::Search with malformed JSON → 400", "[controllers][appointment]")
{
    AppointmentController<ClinicAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Search(std::move(cb), Requester(1, "users"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("AppointmentController<ClinicAppointment>::Search with incomplete data → 406", "[controllers][appointment]")
{
    AppointmentController<ClinicAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Search(std::move(cb), Requester(1, "users"), R"({"foo":"bar"})");
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

// ── Pharmacy / Laboratory / RadiologyCenter ──────────────────────────────────

TEST_CASE("AppointmentController<PharmacyAppointment>::Create rejects malformed JSON", "[controllers][appointment]")
{
    AppointmentController<PharmacyAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Create(std::move(cb), Requester(1, "users"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}

TEST_CASE("AppointmentController<LaboratoryAppointment>::Delete with no id → 406", "[controllers][appointment]")
{
    AppointmentController<LaboratoryAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Delete(std::move(cb), Requester(1, "users"), std::nullopt);
    });
    CHECK(r.status == api::v2::Http::Status::NOT_ACCEPTABLE);
}

TEST_CASE("AppointmentController<RadiologyCenterAppointment>::Search with malformed JSON → 400", "[controllers][appointment]")
{
    AppointmentController<RadiologyCenterAppointment> ctrl;
    auto r = capture([&](auto cb){
        ctrl.Search(std::move(cb), Requester(1, "users"), "bad{");
    });
    CHECK(r.status == api::v2::Http::Status::BAD_REQUEST);
}
