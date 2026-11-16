/**
 * Author: Amr Osman
 * Email: dr3mro@gmail.com
 */

/**
 * @file all_routes.hpp
 * @brief Single inclusion point that registers every API v2 Drogon controller.
 * @details Drogon discovers controllers through the static registration
 * performed by each included header's METHOD_LIST macros; this aggregator has
 * no logic of its own. Include it where the whole v2 route set must be linked
 * in (e.g. application startup), and rely on the per-controller METHOD_ADD
 * declarations for paths, verbs and filter chains. The IWYU pragmas keep the
 * headers as explicit dependencies even though no symbol from them is named
 * here.
 *
 * WHY THIS FILE MATTERS IN THE SERVER:
 * This is the "one-stop shop" for the whole version-2 HTTP API. When the server
 * starts, including this single header pulls in every route controller
 * (appointments, clients, clinics, docs, hello, services and staff). Drogon
 * registers each controller automatically just because its header is seen, so
 * this file is what makes all those URLs actually exist. Without it, none of
 * the v2 endpoints would be reachable and the API would be empty.
 *
 * CONCEPTS TO LEARN (for beginners):
 * - Static registration: simply including a header can register code with a
 * framework, without calling any function yourself.
 * - HTTP filters/middleware: https://drogon.docsforge.com/master/filter/
 * - CORS: https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS
 *
 * @author Amr Osman <dr3mro@gmail.com>
 * @date 2026-11-16
 */
#pragma once  // Include this header at most once per translation unit (prevents
              // duplicate definitions).
// Each include below is kept as an explicit dependency on purpose: Drogon
// registers a controller as a side effect of the header being compiled, even
// though this file never names a symbol from it. The "IWYU pragma: keep"
// markers stop the Include-What-You-Use tool from removing these includes.
#include "api/v2/routes/appointments.hpp"  //IWYU pragma: keep
#include "api/v2/routes/clients.hpp"       //IWYU pragma: keep
#include "api/v2/routes/clinics.hpp"       //IWYU pragma: keep
#include "api/v2/routes/doc.hpp"           //IWYU pragma: keep
#include "api/v2/routes/hello.hpp"         //IWYU pragma: keep
#include "api/v2/routes/services.hpp"      //IWYU pragma: keep
#include "api/v2/routes/staff.hpp"         //IWYU pragma: keep
