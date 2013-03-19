#include <Python.h>
#include <boost/python.hpp>
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/return_value_policy.hpp>
#include "../enki/Types.h"
#include "../enki/Geometry.h"
#include "../enki/PhysicalEngine.h"
#include "../enki/robots/e-puck/EPuck.h"
#include "../viewer/Viewer.h"
#include <QApplication>

using namespace boost::python;
using namespace Enki;

tuple getColorComponents(const Color& color)
{
	return make_tuple(
		color.components[0],
		color.components[1],
		color.components[2],
		color.components[3]
	);
}

void setColorComponents(Color& color, tuple values)
{
	if (len(values) != 4)
		throw std::runtime_error("Tuple used to set components must be of length 4");
	color.components[0] = extract<double>(values[0]);
	color.components[1] = extract<double>(values[1]);
	color.components[2] = extract<double>(values[2]);
	color.components[3] = extract<double>(values[3]);
}

#define def_readwrite_by_value(name, target) \
	add_property(\
		(name), \
		make_getter((target), return_value_policy<return_by_value>()), \
		make_setter((target), return_value_policy<return_by_value>()) \
	)

// vector convertion

struct Vector_to_python_tuple
{
	static PyObject* convert(const Vector& value)
	{
		return incref(make_tuple(value.x, value.y).ptr());
	}
};
struct Vector_from_python
{
	Vector_from_python()
	{
		converter::registry::push_back(
			&convertible,
			&construct,
			type_id<Vector>()
		);
	}
	
	static void* convertible(PyObject* objPtr)
	{
		if (PyTuple_Check(objPtr))
		{
			Py_ssize_t l = PyTuple_Size(objPtr);
			if (l != 2)
				return 0;
			
			PyObject* item0(PyTuple_GetItem(objPtr, 0));
			assert (item0);
			if (!(PyFloat_Check(item0) || PyInt_Check(item0)))
				return 0;
			PyObject* item1(PyTuple_GetItem(objPtr, 1));
			assert (item1);
			if (!(PyFloat_Check(item1) || PyInt_Check(item1)))
				return 0;
		}
		else
		{
			Py_ssize_t l = PyObject_Length(objPtr);
			if (l != 2)
				return 0;
			
			PyObject* item0(PyList_GetItem(objPtr, 0));
			assert (item0);
			if (!(PyFloat_Check(item0) || PyInt_Check(item0)))
				return 0;
			PyObject* item1(PyList_GetItem(objPtr, 1));
			assert (item1);
			if (!(PyFloat_Check(item1) || PyInt_Check(item1)))
				return 0;
		}
		
		return objPtr;
	}
	
	static void construct(PyObject* objPtr, converter::rvalue_from_python_stage1_data* data)
	{
		double x,y;
		
		if (PyTuple_Check(objPtr))
		{
			x = PyFloat_AsDouble(PyTuple_GetItem(objPtr, 0));
			y = PyFloat_AsDouble(PyTuple_GetItem(objPtr, 1));
		}
		else
		{
			x = PyFloat_AsDouble(PyList_GetItem(objPtr, 0));
			y = PyFloat_AsDouble(PyList_GetItem(objPtr, 1));
		}
		
		void* storage = ((converter::rvalue_from_python_storage<Vector>*)data)->storage.bytes;
		new (storage) Vector(x,y);
		data->convertible = storage;
	}
};

// wrappers for objects

struct CircularPhysicalObject: public PhysicalObject
{
	CircularPhysicalObject(double radius, double height, double mass, const Color& color = Color())
	{
		setCylindric(radius, height, mass);
		setColor(color);
	}
};

struct RectangularPhysicalObject: public PhysicalObject
{
	RectangularPhysicalObject(double l1, double l2, double height, double mass, const Color& color = Color())
	{
		setRectangular(l1, l2, height, mass);
		setColor(color);
	}
};

// wrappers for robots

struct EPuckWrap: EPuck, wrapper<EPuck>
{
	EPuckWrap():
		EPuck(CAPABILITY_BASIC_SENSORS|CAPABILITY_CAMERA)
	{}
	
	virtual void controlStep(double dt)
	{
		if (override controlStep = this->get_override("controlStep"))
			controlStep(dt);
		
		EPuck::controlStep(dt);
	}
	
	list getProxSensorValues(void)
	{
		list l;
		l.append(infraredSensor0.finalValue);
		l.append(infraredSensor1.finalValue);
		l.append(infraredSensor2.finalValue);
		l.append(infraredSensor3.finalValue);
		l.append(infraredSensor4.finalValue);
		l.append(infraredSensor5.finalValue);
		l.append(infraredSensor6.finalValue);
		l.append(infraredSensor7.finalValue);
		return l;
	}
	
	list getProxSensorDistances(void)
	{
		list l;
		l.append(infraredSensor0.getDist());
		l.append(infraredSensor1.getDist());
		l.append(infraredSensor2.getDist());
		l.append(infraredSensor3.getDist());
		l.append(infraredSensor4.getDist());
		l.append(infraredSensor5.getDist());
		l.append(infraredSensor6.getDist());
		l.append(infraredSensor7.getDist());
		return l;
	}
	
	Texture getCameraImage(void)
	{
		Texture texture;
		texture.reserve(camera.image.size());
		for (size_t i = 0; i < camera.image.size(); ++i)
			texture.push_back(camera.image[i]);
		return texture;
	}
};

struct PythonViewer: public ViewerWidget
{
	PythonViewer(World& world, Vector camPos, double camAltitude, double camYaw, double camPitch):
		ViewerWidget(&world)
	{
		pos.setX(-camPos.x);
		pos.setY(-camPos.y);
		altitude = camAltitude;
		yaw = -camYaw;
		pitch = -camPitch;
		
		managedObjectsAliases[&typeid(EPuckWrap)] = &typeid(EPuck);
	}
	
	void sceneCompletedHook()
	{
		glColor3d(0,0,0);
		renderText(10, height()-50, tr("rotate camera by moving mouse while pressing ctrl+left mouse button"));
		renderText(10, height()-30, tr("move camera on x/y by moving mouse while pressing ctrl+shift+left mouse button"));
		renderText(10, height()-10, tr("move camera on z by moving mouse while pressing ctrl+shift+right mouse button"));
	}
};

void runInViewer(World& world, Vector camPos = Vector(0,0), double camAltitude = 0, double camYaw = 0, double camPitch = 0)
{
	int argc(1);
	char* argv[1] = {(char*)"dummy"}; // FIXME: recovery sys.argv
	QApplication app(argc, argv);
	PythonViewer viewer(world, camPos, camAltitude, camYaw, camPitch);
	viewer.setWindowTitle("PyEnki Viewer");
	viewer.show();
	app.exec();
}

void run(World& world, unsigned steps)
{
	for (unsigned i = 0; i < steps; ++i)
		world.step(1./30., 3);
}

BOOST_PYTHON_MEMBER_FUNCTION_OVERLOADS(step_overloads, step, 1, 2)
BOOST_PYTHON_FUNCTION_OVERLOADS(runInViewer_overloads, runInViewer, 1, 5)

BOOST_PYTHON_MODULE(pyenki)
{
	// setup converters
	to_python_converter<Vector, Vector_to_python_tuple>();
	Vector_from_python();
	
	// setup Enki
	World::takeObjectOwnership(false);
	
	// TODO: complete doc
	
	// Color and texture
	
	class_<Color>("Color",
		"A color in RGBA",
		init<optional<double, double, double, double> >(
			"Create a RGBA color.\n\n"
			"Arguments:\n"
			"    r -- red component [0..1], default: 0.0\n"
			"    g -- green component [0..1], default: 0.0\n"
			"    b -- blue component [0..1], default: 0.0\n"
			"    a -- alpha (transparency) component [0..1], default: 1.0\n",
			args("r", "g", "b", "a")
		)
	)
		.def(self += double())
		.def(self + double())
		.def(self -= double())
		.def(self - double())
		.def(self *= double())
		.def(self * double())
		.def(self /= double())
		.def(self / double())
		.def(self += self)
		.def(self + self)
		.def(self -= self)
		.def(self - self)
		.def(self == self)
		.def(self != self)
		.def(self_ns::str(self_ns::self))
		.def("threshold", &Color::threshold)
		.def("toGray", &Color::toGray)
		.def_readonly("black", &Color::black)
		.def_readonly("white", &Color::white)
		.def_readonly("gray", &Color::gray)
		.def_readonly("red", &Color::red)
		.def_readonly("green", &Color::green)
		.def_readonly("blue", &Color::blue)
		.add_property("r", &Color::r, &Color::setR)
		.add_property("g", &Color::g, &Color::setG)
		.add_property("b", &Color::b, &Color::setB)
		.add_property("a", &Color::a, &Color::setA)
		.add_property("components", getColorComponents, setColorComponents)
	;
	
	class_<Texture>("Texture")
		.def(vector_indexing_suite<Texture>())
	;
	
	class_<Textures>("Textures")
		.def(vector_indexing_suite<Textures>())
	;
	
	// Physical objects
	
	class_<PhysicalObject>("PhysicalObject")
		.def_readonly("radius", &PhysicalObject::getRadius)
		.def_readonly("height", &PhysicalObject::getHeight)
		.def_readonly("isCylindric", &PhysicalObject::isCylindric)
		.def_readonly("mass", &PhysicalObject::getMass)
		.def_readonly("momentOfInertia", &PhysicalObject::getMomentOfInertia)
		.def_readwrite("collisionElasticity", &PhysicalObject::collisionElasticity)
		.def_readwrite("dryFrictionCoefficient", &PhysicalObject::dryFrictionCoefficient)
		.def_readwrite("viscousFrictionCoefficient", &PhysicalObject::viscousFrictionCoefficient)
		.def_readwrite("viscousMomentFrictionCoefficient", &PhysicalObject::viscousMomentFrictionCoefficient)
		.def_readwrite_by_value("pos", &PhysicalObject::pos)
		.def_readwrite("angle", &PhysicalObject::angle)
		.def_readwrite_by_value("speed", &PhysicalObject::speed)
		.def_readwrite("angSpeed", &PhysicalObject::angSpeed)
		.add_property("color",  make_function(&PhysicalObject::getColor, return_value_policy<copy_const_reference>()), &PhysicalObject::setColor)
		.add_property("infraredReflectiveness", &PhysicalObject::getInfraredReflectiveness, &PhysicalObject::setInfraredReflectiveness)
	;
	
	class_<CircularPhysicalObject, bases<PhysicalObject> >("CircularObject",
		init<double, double, double, optional<const Color&> >(args("radius", "height", "mass", "color"))
	);
	
	class_<RectangularPhysicalObject, bases<PhysicalObject> >("RectangularObject",
		init<double, double, double, double, optional<const Color&> >(args("l1", "l2", "height", "mass", "color"))
	);
	
	// Robots
	
	class_<Robot, bases<PhysicalObject> >("PhysicalObject")
	;
	
	class_<DifferentialWheeled, bases<Robot> >("DifferentialWheeled", no_init)
		.def_readwrite("leftSpeed", &DifferentialWheeled::leftSpeed)
		.def_readwrite("rightSpeed", &DifferentialWheeled::rightSpeed)
		.def_readonly("leftEncoder", &DifferentialWheeled::leftEncoder)
		.def_readonly("rightEncoder", &DifferentialWheeled::rightEncoder)
		.def_readonly("leftOdometry", &DifferentialWheeled::leftOdometry)
		.def_readonly("rightOdometry", &DifferentialWheeled::rightOdometry)
		.def("resetEncoders", &DifferentialWheeled::resetEncoders)
	;
	
	class_<EPuckWrap, bases<DifferentialWheeled>, boost::noncopyable>("EPuck")
		.def("controlStep", &EPuckWrap::controlStep)
		.def_readonly("proximitySensorValues", &EPuckWrap::getProxSensorValues)
		.def_readonly("proximitySensorDistances", &EPuckWrap::getProxSensorDistances)
		.def_readonly("cameraImage", &EPuckWrap::getCameraImage)
	;
	
	// World
	
	class_<World>("World",
		"The world is the container of all objects and robots.\n"
		"It is either a rectangular arena with walls at all sides, a circular area with walls, or an infinite surface."
		,
		init<double, double, optional<const Color&> >(args("width", "height", "wallsColor"))
	)
		.def(init<double, optional<const Color&> >(args("r", "wallsColor")))
		.def(init<>())
		.def("step", &World::step, step_overloads(args("dt", "physicsOversampling")))
		.def("addObject", &World::addObject, with_custodian_and_ward<1,2>())
		.def("removeObject", &World::removeObject)
		.def("setRandomSeed", &World::setRandomSeed)
		.def("run", run)
		.def("runInViewer", runInViewer, runInViewer_overloads(args("self", "camPos", "camAltitude", "camYaw", "camPitch")))
	;
	
	// TODO: add viewer
}