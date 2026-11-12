"""Step definitions for the in-page test console; mirrors test_api.sh order."""
import os, re

def build_steps(payload_dir):
    def tpl(name):
        return open(os.path.join(payload_dir, name + ".json")).read()
    steps = []
    def S(section, name, method, path, *, query=None, auth=None, body=None, expect=200, saves=None, set=None, needs=None,
          optional=False, cleanup=False, clears=None):
        sid = re.sub(r"[^a-z0-9]+", "-", (section[:3] + " " + name).lower()).strip("-")
        base, n = sid, 2
        while any(x["id"] == sid for x in steps):
            sid = "%s-%d" % (base, n); n += 1
        steps.append({"id": sid, "section": section, "name": name, "method": method, "path": path, "query": query, "auth": auth,
                      "body": body, "expect": expect, "saves": saves or {}, "set": set or {}, "needs": needs or [], "optional": optional,
                      "cleanup": cleanup, "clears": clears or []})
    READ = lambda var: dict(body=tpl("common/read"), set={"ID": "{{%s}}" % var})
    LOGIN = lambda sec, grp, un, pw, tokvar, idvar, needs, saves_id=True: S(
        sec, grp + " login", "POST", "/clients/%s/login" % grp, body=tpl("auth/login"),
        set={"LOGIN_USERNAME": un, "LOGIN_PASSWORD": pw}, needs=needs,
        saves=dict([(tokvar, "token")] + ([(idvar, "client_id")] if saves_id else [])))

    S("Health check", "hello", "GET", "/hello")

    for sec, grp, P, U in (("Provider", "providers", "PROVIDER", "PROVIDER"), ("User", "users", "USER", "USER")):
        S(sec, grp + " create", "POST", "/clients/%s/create" % grp, body=tpl(grp + "/create"), saves={P + "_ID": "id"})
        LOGIN(sec, grp, "{{%s_USERNAME}}" % P, "{{%s_PASSWORD}}" % P, P + "_TOKEN", P + "_ID", [P + "_ID"])
        S(sec, grp + " read", "POST", "/clients/%s/read" % grp, auth=P + "_TOKEN", **READ(P + "_ID"))
        S(sec, grp + " update", "PUT", "/clients/%s/update" % grp, query="id={{%s_ID}}" % P, auth=P + "_TOKEN", body=tpl(grp + "/update"))
        S(sec, grp + " search", "POST", "/clients/%s/search" % grp, auth=P + "_TOKEN", body=tpl(grp + "/search"))
        S(sec, grp + " services", "GET", "/clients/%s/services" % grp, query="id={{%s_ID}}" % P, auth=P + "_TOKEN")
        S(sec, grp + " activate", "POST", "/clients/%s/activate" % grp, query="id={{%s_ID}}" % P, auth=P + "_TOKEN")

    PT = "PROVIDER_TOKEN"
    def crud(sec, seg, base, var):
        S(sec, seg + " create", "POST", base + "/create", auth=PT, body=tpl(seg + "/create"), saves={var: "id"})
        S(sec, seg + " read", "POST", base + "/read", auth=PT, **READ(var))
        S(sec, seg + " update", "PUT", base + "/update", query="id={{%s}}" % var, auth=PT, body=tpl(seg + "/update"))
        S(sec, seg + " search", "POST", base + "/search", auth=PT, body=tpl(seg + "/search"))
    crud("Services: clinic", "clinics", "/services/clinics", "CLINIC_ID")
    sec = "Services: pharmacy, laboratory, radiology center"
    crud(sec, "pharmacies", "/services/pharmacies", "PHARMACY_ID")
    crud(sec, "laboratories", "/services/laboratories", "LAB_ID")
    crud(sec, "radiologycenters", "/services/radiologycenters", "RAD_ID")

    sec = "Clinic: patient"
    crud(sec, "patients", "/clinic/patients", "PATIENT_ID")
    S(sec, "patients getvisits", "GET", "/clinic/patients/getvisits", query="patient_id={{PATIENT_ID}}", auth=PT)

    def rec(sec, seg, parent, saveid):
        b = "/clinic/" + seg
        S(sec, seg + " create", "POST", b + "/create", auth=PT, body=tpl(seg + "/create"), saves={saveid: "id"})
        S(sec, seg + " read", "POST", b + "/read", auth=PT, needs=[saveid], **READ(parent))
        S(sec, seg + " update", "PUT", b + "/update", query="id={{%s}}" % parent, auth=PT, body=tpl(seg + "/update"), needs=[saveid])
    sec = "Clinic: patient health, drugs, reports"
    rec(sec, "health", "PATIENT_ID", "HEALTH_ID")
    rec(sec, "patientdrugs", "PATIENT_ID", "DRUGS_ID")
    rec(sec, "reports", "PATIENT_ID", "REPORTS_ID")

    sec = "Clinic: visit and its requests, prescriptions, paid services"
    rec(sec, "visits", "VISIT_ID", "VISIT_ID")
    for seg, var in (("requests", "REQUEST_ID"), ("prescriptions", "PRESCRIPTION_ID"), ("paidservices", "PAIDSERVICE_ID")):
        rec(sec, seg, "VISIT_ID", var)

    sec = "Appointments (needs clinic, user and provider)"
    S(sec, "appointments create", "POST", "/appointments/clinics/create", auth=PT, body=tpl("appointments/create"), saves={"APPOINTMENT_ID": "id"},
      needs=["CLINIC_ID", "USER_ID", "PROVIDER_ID"])
    S(sec, "appointments read", "POST", "/appointments/clinics/read", query="id={{APPOINTMENT_ID}}", auth=PT, body=tpl("appointments/read"))
    S(sec, "appointments update", "PUT", "/appointments/clinics/update", query="id={{APPOINTMENT_ID}}", auth=PT, body=tpl("appointments/update"))

    sec = "Suspend (disposable accounts, left suspended)"
    for grp, D, P in (("providers", "DISP_PROVIDER", "PD"), ("users", "DISP_USER", "UD")):
        S(sec, grp + " create (disposable)", "POST", "/clients/%s/create" % grp, body=tpl(grp + "/disposable"), set={"DISPOSABLE_NAME": "{{%s}}" % D},
          saves={P + "_ID": "id"}, optional=True)
        LOGIN(sec, grp, "{{%s}}" % D, "{{USER_PASSWORD}}", P + "_TOKEN", None, [P + "_ID"], saves_id=False)
        steps[-1]["optional"] = True
        S(sec, grp + " activate", "POST", "/clients/%s/activate" % grp, query="id={{%s_ID}}" % P, auth=P + "_TOKEN", optional=True)
        S(sec, grp + " suspend", "POST", "/clients/%s/suspend" % grp, query="id={{%s_ID}}" % P, auth=P + "_TOKEN", optional=True)
        S(sec, grp + " blocked while suspended", "POST", "/clients/%s/read" % grp, auth=P + "_TOKEN", expect=401, optional=True, **READ(P + "_ID"))

    sec = "Clean-up (reverse order)"
    nd = " (not implemented by design)"
    d = lambda name, path, var, expect=200: S(sec, name, "DELETE", path, query="id={{%s}}" % var, auth=PT, expect=expect, cleanup=True,
                                              needs=[var.replace("VISIT_ID", "VISIT_ID")])
    d("paidservices delete" + nd, "/clinic/paidservices/delete", "VISIT_ID", 400)
    d("prescriptions delete" + nd, "/clinic/prescriptions/delete", "VISIT_ID", 400)
    d("requests delete" + nd, "/clinic/requests/delete", "VISIT_ID", 400)
    d("visits delete", "/clinic/visits/delete", "VISIT_ID")
    d("reports delete" + nd, "/clinic/reports/delete", "PATIENT_ID", 400)
    d("patientdrugs delete" + nd, "/clinic/patientdrugs/delete", "PATIENT_ID", 400)
    d("health delete" + nd, "/clinic/health/delete", "PATIENT_ID", 400)
    d("patients delete", "/clinic/patients/delete", "PATIENT_ID")
    d("appointments delete", "/appointments/clinics/delete", "APPOINTMENT_ID")
    d("clinics delete", "/services/clinics/delete", "CLINIC_ID")
    d("pharmacies delete", "/services/pharmacies/delete", "PHARMACY_ID")
    d("laboratories delete", "/services/laboratories/delete", "LAB_ID")
    d("radiologycenters delete", "/services/radiologycenters/delete", "RAD_ID")
    for grp, P in (("users", "USER"), ("providers", "PROVIDER")):
        S(sec, grp + " logout", "POST", "/clients/%s/logout" % grp, auth=P + "_TOKEN", body="@empty", cleanup=True)
        S(sec, grp + " login (fresh token)", "POST", "/clients/%s/login" % grp, body=tpl("auth/login"), cleanup=True, needs=[P + "_ID"],
          set={"LOGIN_USERNAME": "{{%s_USERNAME}}" % P, "LOGIN_PASSWORD": "{{%s_PASSWORD}}" % P}, saves={P + "_TOKEN": "token"})
        S(sec, grp + " delete", "DELETE", "/clients/%s/delete" % grp, query="id={{%s_ID}}" % P, auth=P + "_TOKEN", cleanup=True)
    return steps
