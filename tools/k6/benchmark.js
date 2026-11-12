// Valhalla API benchmark (k6) - generated from the Insomnia "Valhalla Project" collection.
//
//   k6 run tools/k6/benchmark.js                      # full run (reads + crud + auth scenarios)
//   SMOKE=1 k6 run tools/k6/benchmark.js              # 1 VU, 10s sanity check
//   BASE_URL=http://host:8080 k6 run tools/k6/benchmark.js
//
// Env overrides: BASE_URL, PROVIDER_USER, PROVIDER_PASS, USER_NAME, USER_PASS, SMOKE
//
// NOTE: your rate limiter / blacklist (.env: RL_*, BAN_*, WHITELIST, BLACKLIST) will start
// returning 429/403 under load. Whitelist the load generator's IP before benchmarking.

import http from 'k6/http';
import { check, fail } from 'k6';

const BASE = (__ENV.BASE_URL || 'http://127.0.0.1:8080') + '/api/v2';
const PROVIDER = { username: __ENV.PROVIDER_USER || 'provider', password: __ENV.PROVIDER_PASS || '1234@Abc!' };
const USER = { username: __ENV.USER_NAME || 'user1', password: __ENV.USER_PASS || '123Mm@p0' };
const SMOKE = !!__ENV.SMOKE;

export const options = {
  scenarios: SMOKE
    ? { smoke: { executor: 'constant-vus', exec: 'reads', vus: 1, duration: '10s' } }
    : {
        // read-heavy traffic: read/search/list endpoints of every entity
        reads: {
          executor: 'ramping-vus',
          exec: 'reads',
          startVUs: 0,
          stages: [
            { duration: '15s', target: 50 },
            { duration: '60s', target: 100 },
            { duration: '15s', target: 0 },
          ],
        },
        // write traffic: full create -> read -> update -> search -> delete lifecycles
        crud: { executor: 'constant-vus', exec: 'crud', vus: 10, duration: '90s' },
        // login endpoints (password hashing + token issuing)
        auth: {
          executor: 'constant-arrival-rate',
          exec: 'auth',
          rate: 5,
          timeUnit: '1s',
          duration: '90s',
          preAllocatedVUs: 10,
        },
      },
  thresholds: {
    http_req_failed: ['rate<0.01'],
    http_req_duration: ['p(95)<500'],
    'http_req_duration{scenario:reads}': ['p(95)<300'],
    'http_req_duration{scenario:crud}': ['p(95)<800'],
    checks: ['rate>0.99'],
  },
  summaryTrendStats: ['avg', 'min', 'med', 'p(90)', 'p(95)', 'p(99)', 'max'],
};

// ---------------------------------------------------------------- helpers
function call(method, path, body, token, name) {
  const headers = { 'Content-Type': 'application/json' };
  if (token) headers.Authorization = `Bearer ${token}`;
  const payload = body === undefined || body === null ? null : JSON.stringify(body);
  return http.request(method, `${BASE}${path}`, payload, { headers, tags: { name } });
}

function ok(res, name) {
  return check(res, { [`${name} 200`]: (r) => r.status === 200 });
}

function jsonField(res, field) {
  try {
    return res.json(field);
  } catch (e) {
    return undefined;
  }
}

function mustId(res, what) {
  const id = jsonField(res, 'id');
  if (id === undefined || res.status !== 200) {
    fail(`setup: could not create ${what} (status ${res.status}): ${String(res.body).slice(0, 200)}`);
  }
  return id;
}

function pick(items) {
  const total = items.reduce((s, i) => s + i[0], 0);
  let r = Math.random() * total;
  for (const [w, fn] of items) {
    r -= w;
    if (r <= 0) return fn;
  }
  return items[items.length - 1][1];
}

// ---------------------------------------------------------------- payloads (from the collection)
const providerBody = {
  avatar: 'http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200',
  active: true, language: 'en', username: PROVIDER.username, password: PROVIDER.password,
  fullname: 'Amr Nasr', phone: '+201299999999', email: 'amr@mail.com', country: 'Egypt',
  city: 'Damietta', address: 'portsaid street', dob: '17-02-1985', gender: 'Male',
  position: 'Doctor', speciality: 'Cardiology',
};
const userBody = {
  avatar: 'http://www.gravatar.com/avatar/205e460b479e2e5b48aec07710c08d50?s=200',
  language: 'en', active: true, parent: '0', username: USER.username, email: 'amr@mail.com',
  password: USER.password, fullname: 'Amr Nasr', phone: '+201299999999', country: 'Egypt',
  city: 'Damietta', address: '12 valhalla Street ', dob: '10-10-1990', gender: 'Male',
  marital: 'single', job: 'Doctor',
};
const LOGO = 'https://www.clipartmax.com/png/small/150-1509965_entity-gaming-entity-gaming-logo.png';
const orgBody = (name, providerId) => ({
  logo: LOGO, name, country: 'Egypt', city: 'Damietta', address: '11 street Ras elbar',
  phone: ['+1987654321'], admin_id: providerId, owner_id: providerId,
  staff: { Doctors: ['1001', '1921'], nurses: [''], assistants: ['1000'] },
});
const clinicBody = (providerId) => ({
  logo: LOGO, name: 'Clinic0', specialities: ['Cardiology', 'Medicine'], country: 'Egypt',
  city: 'Damietta', address: '11 street Ras elbar', phone: ['+1987654321'],
  admin_id: providerId, owner_id: providerId,
  staff: { Doctors: [`${providerId}:62`], nurses: [''], assistants: [''] },
});
const patientBody = (clinicId) => ({
  email: 'amr@mail.com', clinic_id: clinicId, fullname: 'Amr Nasr', phone: '+201299999999',
  city: 'Damietta', address: '11 Valhalla street', dob: '17-02-1985', gender: 'Male',
  marital: 'Single', job: 'Doctor',
});
const patientUpdateBody = {
  email: 'amr@mail.com', fullname: 'Amr Nasr Osman', phone: '+201299999999', city: 'Damietta',
  address: '11 Valhalla street', dob: '22-10-1990', gender: 'male', marital: 'single', job: 'Doctor',
};
const healthFields = [
  'past_medical_history', 'allergies', 'medications', 'surgeries', 'pregnancy', 'breastfeeding',
  'mammogram', 'blood_pressure', 'blood_sugar', 'family_planning', 'hiv', 'hepatitis',
  'renal_disease', 'tuberculosis', 'cancer', 'diabetes', 'asthma', 'hypertension',
  'chronic_cardiac_disease', 'stroke', 'kidney_disease', 'liver_disease', 'immunizations',
  'nutrition', 'physical_exam', 'sleep_apnea', 'mental_health', 'tobacco_smoking', 'alcohol',
  'drugs', 'other', 'pregnancy_complications', 'past_treatments', 'family_history',
  'vaccinations', 'other_treatments', 'comments',
];
const health = Object.fromEntries(healthFields.map((f) => [f, 'No']));
const drug = (name) => ({
  name, dosage: '1 tablet', frequency: 'daily', duration: '7 days', route: 'oral', quantity: '1',
  start_date: '2020-01-01', end_date: '2020-01-31', prescription: 'No', dispense: 'No',
  other: 'No', comments: 'No',
});
const drugs = [drug('Aspirin'), drug('Paracetamol')];
const reports = [
  { report_type: 'Clinical', name: 'Preoperative', date: '2020-01-01', result: 'Text' },
  { report_type: 'Clinical', name: 'Preoccupation', date: '2020-01-01', result: 'Text' },
];
const requests = [
  { name: 'Chest Xray', date: '2020-01-01', result: 'Positive for lung cancer', media: 'url' },
  { name: 'CBC', date: '2020-01-01', result: 'Anemia', media: 'url' },
];
const paidservices = [
  { name: 'Service 1', date: '2020-01-01', amount: '100.00' },
  { name: 'Service 2', date: '2020-01-01', amount: '100.00' },
];
const visitBody = (patientId, clinicId) => ({
  patient_id: patientId, clinic_id: clinicId, visit_date: '2020-01-01', visit_time: '10:00',
  complaints: 'Headache', symptoms: 'Rhinorhea, sorethroat',
  examination: {
    BP: '120/80', pulse: '100', temperature: '98.6', respiration: '18', oxygen_saturation: '98',
    weight: '80', height: '180', comments: 'No', Examination: '', Other: '', Notes: '',
  },
  diagnosis: 'Flu', comments: 'None',
});
const appointmentBody = (clinicId, userId, providerId) => ({
  clinic_id: clinicId, user_id: userId, provider_id: providerId,
  appointment_date: '2026-05-25', appointment_time: '10:00:00', appointment_duration: 30,
  appointment_reason: 'Check-up', appointment_note: 'No notes',
  appointment_payment_transaction_id: '1000', appointment_payment_transaction_status: 'pending',
  appointment_payment_method: 'amount', appointment_payment_currency: 'currency',
  appointment_payment_date: '2026-05-25', appointment_payment_time: '10:00:00',
  appointment_payment_amount: 100, appointment_payment_status: 'pending',
  appointment_type: 'regular', appointment_status: 'pending',
});
const search = (keyword, filter) => ({ keyword, filter, limit: 5, offset: 0, order_by: 'id', direction: 0 });

// ---------------------------------------------------------------- setup / teardown
export function setup() {
  // register (ignore failure: they may already exist), then log in
  call('POST', '/clients/providers/create', providerBody, null, 'setup');
  call('POST', '/clients/users/create', userBody, null, 'setup');

  const pl = call('POST', '/clients/providers/login', PROVIDER, null, 'setup');
  if (pl.status !== 200) fail(`provider login failed (${pl.status}): ${String(pl.body).slice(0, 200)}`);
  const providerToken = pl.json('token');
  const providerId = pl.json('client_id');

  const ul = call('POST', '/clients/users/login', USER, null, 'setup');
  if (ul.status !== 200) fail(`user login failed (${ul.status}): ${String(ul.body).slice(0, 200)}`);
  const userToken = ul.json('token');
  const userId = ul.json('client_id');

  const t = providerToken;
  const clinicId = mustId(call('POST', '/services/clinics/create', clinicBody(providerId), t, 'setup'), 'clinic');
  const pharmacyId = mustId(call('POST', '/services/pharmacies/create', orgBody('Pharmacy 101', providerId), t, 'setup'), 'pharmacy');
  const laboratoryId = mustId(call('POST', '/services/laboratories/create', orgBody('Clinical Lab 101', providerId), t, 'setup'), 'laboratory');
  const radiologyId = mustId(call('POST', '/services/radiologycenters/create', orgBody('RadiologyCenter 101', providerId), t, 'setup'), 'radiology center');
  const patientId = mustId(call('POST', '/clinic/patients/create', patientBody(clinicId), t, 'setup'), 'patient');
  mustId(call('POST', '/clinic/health/create', { id: patientId, clinic_id: clinicId, health }, t, 'setup'), 'health');
  mustId(call('POST', '/clinic/patientdrugs/create', { id: patientId, clinic_id: clinicId, drugs }, t, 'setup'), 'patient drugs');
  mustId(call('POST', '/clinic/reports/create', { id: patientId, clinic_id: clinicId, reports }, t, 'setup'), 'reports');
  const visitId = mustId(call('POST', '/clinic/visits/create', visitBody(patientId, clinicId), t, 'setup'), 'visit');
  mustId(call('POST', '/clinic/requests/create', { id: visitId, clinic_id: clinicId, requests }, t, 'setup'), 'requests');
  mustId(call('POST', '/clinic/prescriptions/create', { id: visitId, clinic_id: clinicId, prescriptions: drugs }, t, 'setup'), 'prescriptions');
  mustId(call('POST', '/clinic/paidservices/create', { id: visitId, clinic_id: clinicId, paidservices }, t, 'setup'), 'paid services');
  const appointmentId = mustId(call('POST', '/appointments/clinics/create', appointmentBody(clinicId, userId, providerId), t, 'setup'), 'appointment');

  return {
    providerToken, providerId, userToken, userId, clinicId, pharmacyId, laboratoryId,
    radiologyId, patientId, visitId, appointmentId,
  };
}

export function teardown(d) {
  const t = d.providerToken;
  const del = (path) => call('DELETE', path, null, t, 'teardown');
  del(`/appointments/clinics/delete?id=${d.appointmentId}`);
  del(`/clinic/paidservices/delete?id=${d.visitId}`);
  del(`/clinic/prescriptions/delete?id=${d.visitId}`);
  del(`/clinic/requests/delete?id=${d.visitId}`);
  del(`/clinic/visits/delete?id=${d.visitId}`);
  del(`/clinic/reports/delete?id=${d.patientId}`);
  del(`/clinic/patientdrugs/delete?id=${d.patientId}`);
  del(`/clinic/health/delete?id=${d.patientId}`);
  del(`/clinic/patients/delete?id=${d.patientId}`);
  del(`/services/radiologycenters/delete?id=${d.radiologyId}`);
  del(`/services/laboratories/delete?id=${d.laboratoryId}`);
  del(`/services/pharmacies/delete?id=${d.pharmacyId}`);
  del(`/services/clinics/delete?id=${d.clinicId}`);
  call('DELETE', `/clients/users/delete?id=${d.userId}`, null, d.userToken, 'teardown');
  del(`/clients/providers/delete?id=${d.providerId}`);
}

// ---------------------------------------------------------------- scenario: reads
export function reads(d) {
  const p = d.providerToken;
  const u = d.userToken;
  const mix = [
    [3, () => ok(call('GET', '/hello', null, null, 'hello'), 'hello')],
    [6, () => ok(call('POST', '/clients/providers/read', { id: d.providerId, schema: [] }, p, 'providers/read'), 'providers/read')],
    [4, () => ok(call('POST', '/clients/providers/search', search('cardiology', 'speciality'), p, 'providers/search'), 'providers/search')],
    [3, () => ok(call('GET', `/clients/providers/services?id=${d.providerId}`, null, p, 'providers/services'), 'providers/services')],
    [6, () => ok(call('POST', '/clients/users/read', { id: d.userId, schema: [] }, u, 'users/read'), 'users/read')],
    [4, () => ok(call('POST', '/clients/users/search', search('amr', 'fullname'), u, 'users/search'), 'users/search')],
    [3, () => ok(call('GET', `/clients/users/services?id=${d.userId}`, null, u, 'users/services'), 'users/services')],
    [5, () => ok(call('POST', '/services/clinics/read', { id: d.clinicId, schema: [] }, p, 'clinics/read'), 'clinics/read')],
    [3, () => ok(call('POST', '/services/clinics/search', search('cardiology', 'specialities'), p, 'clinics/search'), 'clinics/search')],
    [3, () => ok(call('POST', '/services/pharmacies/read', { id: d.pharmacyId, schema: [] }, p, 'pharmacies/read'), 'pharmacies/read')],
    [2, () => ok(call('POST', '/services/pharmacies/search', search('pharma', 'name'), p, 'pharmacies/search'), 'pharmacies/search')],
    [3, () => ok(call('POST', '/services/laboratories/read', { id: d.laboratoryId, schema: [] }, p, 'laboratories/read'), 'laboratories/read')],
    [2, () => ok(call('POST', '/services/laboratories/search', search('101', 'name'), p, 'laboratories/search'), 'laboratories/search')],
    [3, () => ok(call('POST', '/services/radiologycenters/read', { id: d.radiologyId, schema: [] }, p, 'radiologycenters/read'), 'radiologycenters/read')],
    [2, () => ok(call('POST', '/services/radiologycenters/search', search('101', 'name'), p, 'radiologycenters/search'), 'radiologycenters/search')],
    [8, () => ok(call('POST', '/clinic/patients/read', { id: d.patientId, schema: [] }, p, 'patients/read'), 'patients/read')],
    [4, () => ok(call('POST', '/clinic/patients/search', search('amr', 'fullname'), p, 'patients/search'), 'patients/search')],
    [3, () => ok(call('GET', `/clinic/patients/getvisits?patient_id=${d.patientId}`, null, p, 'patients/getvisits'), 'patients/getvisits')],
    [4, () => ok(call('POST', '/clinic/health/read', { id: d.patientId, schema: [] }, p, 'health/read'), 'health/read')],
    [4, () => ok(call('POST', '/clinic/patientdrugs/read', { id: d.patientId, schema: [] }, p, 'patientdrugs/read'), 'patientdrugs/read')],
    [3, () => ok(call('POST', '/clinic/reports/read', { id: d.patientId, schema: [] }, p, 'reports/read'), 'reports/read')],
    [6, () => ok(call('POST', '/clinic/visits/read', { id: d.visitId, schema: [] }, p, 'visits/read'), 'visits/read')],
    [3, () => ok(call('POST', '/clinic/requests/read', { id: d.visitId, schema: [] }, p, 'requests/read'), 'requests/read')],
    [3, () => ok(call('POST', '/clinic/prescriptions/read', { id: d.visitId, schema: [] }, p, 'prescriptions/read'), 'prescriptions/read')],
    [3, () => ok(call('POST', '/clinic/paidservices/read', { id: d.visitId, schema: [] }, p, 'paidservices/read'), 'paidservices/read')],
    [4, () => ok(call('POST', '/appointments/clinics/read', { id: d.appointmentId, provider_id: d.providerId, clinic_id: d.clinicId, schema: [] }, p, 'appointments/read'), 'appointments/read')],
  ];
  pick(mix)();
}

// ---------------------------------------------------------------- scenario: crud
// Each iteration creates its own patient/visit/appointment so VUs never fight over rows.
export function crud(d) {
  const p = d.providerToken;

  // patient lifecycle
  const created = call('POST', '/clinic/patients/create', patientBody(d.clinicId), p, 'patients/create');
  if (!ok(created, 'patients/create')) return;
  const pid = jsonField(created, 'id');
  ok(call('POST', '/clinic/patients/read', { id: pid, schema: [] }, p, 'patients/read'), 'patients/read');
  ok(call('PUT', `/clinic/patients/update?id=${pid}`, patientUpdateBody, p, 'patients/update'), 'patients/update');
  ok(call('POST', '/clinic/patients/search', search('amr', 'fullname'), p, 'patients/search'), 'patients/search');

  // visit lifecycle for that patient
  const v = call('POST', '/clinic/visits/create', visitBody(pid, d.clinicId), p, 'visits/create');
  if (ok(v, 'visits/create')) {
    const vid = jsonField(v, 'id');
    ok(call('POST', '/clinic/visits/read', { id: vid, schema: [] }, p, 'visits/read'), 'visits/read');
    ok(call('DELETE', `/clinic/visits/delete?id=${vid}`, null, p, 'visits/delete'), 'visits/delete');
  }

  // appointment lifecycle
  const a = call('POST', '/appointments/clinics/create', appointmentBody(d.clinicId, d.userId, d.providerId), p, 'appointments/create');
  if (ok(a, 'appointments/create')) {
    const aid = jsonField(a, 'id');
    ok(call('POST', '/appointments/clinics/read', { id: aid, provider_id: d.providerId, clinic_id: d.clinicId, schema: [] }, p, 'appointments/read'), 'appointments/read');
    ok(call('DELETE', `/appointments/clinics/delete?id=${aid}`, null, p, 'appointments/delete'), 'appointments/delete');
  }

  ok(call('DELETE', `/clinic/patients/delete?id=${pid}`, null, p, 'patients/delete'), 'patients/delete');
}

// ---------------------------------------------------------------- scenario: auth
export function auth() {
  if (Math.random() < 0.5) {
    ok(call('POST', '/clients/providers/login', PROVIDER, null, 'providers/login'), 'providers/login');
  } else {
    ok(call('POST', '/clients/users/login', USER, null, 'users/login'), 'users/login');
  }
}
