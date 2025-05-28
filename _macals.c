/**
 * MIT License
 *
 * Copyright (c) Matt Martz <matt@sivel.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <Python.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

static PyTypeObject LightSensorType;
static PyTypeObject LightSensorIteratorType;

typedef struct {
    PyObject_HEAD
    io_service_t service;
    char service_name[128];
} LightSensorObject;

typedef struct {
    PyObject_HEAD
    io_iterator_t iter;
} LightSensorIterator;

static void LightSensor_dealloc(LightSensorObject* self) {
    if (self->service != MACH_PORT_NULL) {
        IOObjectRelease(self->service);
        self->service = MACH_PORT_NULL;
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* LightSensor_get_current_lux(LightSensorObject* self, PyObject* Py_UNUSED(ignored)) {
    if (!self->service) {
        PyErr_SetString(PyExc_RuntimeError, "No valid sensor service.");
        return NULL;
    }

    CFTypeRef luxValue = IORegistryEntryCreateCFProperty(self->service, CFSTR("CurrentLux"), kCFAllocatorDefault, 0);
    if (!luxValue) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get CurrentLux property.");
        return NULL;
    }

    float lux = 0;
    if (CFGetTypeID(luxValue) == CFNumberGetTypeID()) {
        CFNumberGetValue((CFNumberRef)luxValue, kCFNumberFloatType, &lux);
    } else {
        CFRelease(luxValue);
        PyErr_SetString(PyExc_RuntimeError, "CurrentLux is not a number.");
        return NULL;
    }

    CFRelease(luxValue);
    return PyFloat_FromDouble(lux);
}

static PyObject* LightSensor_repr(LightSensorObject* self) {
    return PyUnicode_FromFormat("LightSensor('%s')", self->service_name);
}

static int LightSensor_init(LightSensorObject* self, PyObject* args, PyObject* kwds) {
    const char* name = NULL;
    if (!PyArg_ParseTuple(args, "s", &name)) {
        PyErr_SetString(PyExc_TypeError, "Expected service name as a string.");
        return -1;
    }

    CFMutableDictionaryRef matchingDict = IOServiceMatching("IOService");
    if (!matchingDict) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create matching dictionary.");
        return -1;
    }

    io_iterator_t iter;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iter);
    if (kr != KERN_SUCCESS || !iter) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get matching services.");
        return -1;
    }

    io_service_t service = MACH_PORT_NULL;
    io_service_t candidate = 0;
    char serviceName[128];

    while ((candidate = IOIteratorNext(iter))) {
        kr = IORegistryEntryGetName(candidate, serviceName);
        if (kr == KERN_SUCCESS && strcmp(serviceName, name) == 0) {
            service = candidate;
            break;
        }
        IOObjectRelease(candidate);
    }
    IOObjectRelease(iter);

    if (service == MACH_PORT_NULL) {
        PyErr_SetString(PyExc_RuntimeError, "Service not found.");
        return -1;
    }

    if (self->service != MACH_PORT_NULL) {
        IOObjectRelease(self->service);
    }

    self->service = service;
    snprintf(self->service_name, sizeof(self->service_name), "%s", name);
    return 0;
}

static PyObject* LightSensor_get_name(LightSensorObject* self, void* closure) {
    return PyUnicode_FromString(self->service_name);
}

static PyGetSetDef LightSensor_getset[] = {
    {"name", (getter)LightSensor_get_name, NULL, "service name of the ambient light sensor", NULL},
    {NULL}
};

static PyMethodDef LightSensor_methods[] = {
    {"get_current_lux", (PyCFunction)LightSensor_get_current_lux, METH_NOARGS, PyDoc_STR("Get the lux value of ambient light sensor.")},
    {NULL}
};

static void LightSensorIterator_dealloc(LightSensorIterator* self) {
    if (self->iter) {
        IOObjectRelease(self->iter);
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyObject* LightSensorIterator_next(LightSensorIterator* self) {
    io_service_t service;
    while ((service = IOIteratorNext(self->iter))) {
        CFTypeRef luxValue = IORegistryEntryCreateCFProperty(service, CFSTR("CurrentLux"), kCFAllocatorDefault, 0);
        if (luxValue) {
            char serviceName[128];
            kern_return_t kr = IORegistryEntryGetName(service, serviceName);
            PyObject* sensor = NULL;

            if (kr == KERN_SUCCESS) {
                PyObject* args = PyTuple_Pack(1, PyUnicode_FromString(serviceName));
                if (args) {
                    sensor = PyObject_CallObject((PyObject*)&LightSensorType, args);
                    Py_DECREF(args);
                }
            }

            CFRelease(luxValue);
            IOObjectRelease(service);
            return sensor ? sensor : PyErr_NoMemory();
        }

        IOObjectRelease(service);
    }

    PyErr_SetNone(PyExc_StopIteration);
    return NULL;
}

static PyObject* LightSensorIterator_iter(PyObject* self) {
    Py_INCREF(self);
    return self;
}

static PyTypeObject LightSensorIteratorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_macals._LightSensorIterator",
    .tp_basicsize = sizeof(LightSensorIterator),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Iterator over LightSensor objects (internal use only)",
    .tp_iter = LightSensorIterator_iter,
    .tp_iternext = (iternextfunc)LightSensorIterator_next,
    .tp_dealloc = (destructor)LightSensorIterator_dealloc,
};

static PyObject* py_list_sensors(PyObject* self, PyObject* args) {
    CFMutableDictionaryRef matchingDict = IOServiceMatching("IOService");
    if (!matchingDict) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to create matching dictionary.");
        return NULL;
    }

    io_iterator_t iter;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, matchingDict, &iter);
    if (kr != KERN_SUCCESS || !iter) {
        PyErr_SetString(PyExc_RuntimeError, "Failed to get matching services.");
        return NULL;
    }

    LightSensorIterator* it = PyObject_New(LightSensorIterator, &LightSensorIteratorType);
    if (!it) {
        IOObjectRelease(iter);
        return PyErr_NoMemory();
    }

    it->iter = iter;
    return (PyObject*)it;
}

static PyObject* py_find_sensor(PyObject* self, PyObject* args) {
    PyObject* it = py_list_sensors(self, NULL);
    if (!it) return NULL;

    PyObject* first = PyIter_Next(it);
    Py_DECREF(it);

    if (!first) {
        PyErr_SetString(PyExc_RuntimeError, "No ambient light sensor found.");
        return NULL;
    }

    return first;
}

static PyObject* py_main(PyObject* self, PyObject* args) {
    PyObject* it = py_list_sensors(self, NULL);
    if (!it) return NULL;

    PyObject* sensor;
    while ((sensor = PyIter_Next(it))) {
        PyObject* name_attr = PyObject_GetAttrString(sensor, "name");
        PyObject* lux_attr = PyObject_CallMethod(sensor, "get_current_lux", NULL);
        if (name_attr && lux_attr) {
            printf("%s: %.1f lux\n", PyUnicode_AsUTF8(name_attr), PyFloat_AsDouble(lux_attr));
        }
        Py_XDECREF(name_attr);
        Py_XDECREF(lux_attr);
        Py_DECREF(sensor);
    }

    Py_DECREF(it);
    Py_RETURN_NONE;
}

static PyMethodDef module_methods[] = {
    {"find_sensor", py_find_sensor, METH_NOARGS, PyDoc_STR("Return the first ambient light sensor as a LightSensor object.")},
    {"list_sensors", py_list_sensors, METH_NOARGS, PyDoc_STR("Return an iterator over LightSensor objects.")},
    {"main", py_main, METH_NOARGS, PyDoc_STR("Print names and lux values of all sensors.")},
    {NULL, NULL, 0, NULL}
};

static PyTypeObject LightSensorType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_macals.LightSensor",
    .tp_basicsize = sizeof(LightSensorObject),
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = "Ambient Light Sensor object",
    .tp_methods = LightSensor_methods,
    .tp_getset = LightSensor_getset,
    .tp_dealloc = (destructor)LightSensor_dealloc,
    .tp_repr = (reprfunc)LightSensor_repr,
    .tp_init = (initproc)LightSensor_init,
    .tp_new = PyType_GenericNew,
};

static struct PyModuleDef macalsmodule = {
    PyModuleDef_HEAD_INIT,
    "_macals",
    "Access the ambient light sensor on macOS",
    -1,
    module_methods
};

PyMODINIT_FUNC PyInit__macals(void) {
    if (PyType_Ready(&LightSensorType) < 0) return NULL;
    if (PyType_Ready(&LightSensorIteratorType) < 0) return NULL;

    PyObject* m = PyModule_Create(&macalsmodule);
    if (!m) return NULL;

    Py_INCREF(&LightSensorType);
    PyModule_AddObject(m, "LightSensor", (PyObject*)&LightSensorType);

    return m;
}
