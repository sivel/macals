# macals
Python module for accessing the ambient light sensor on macOS

## Usage

### Command Line

```
python -m macals
```

This will iterate all ambient light sensors and their lux values.

### Python

```python
from macals import list_sensors

for sensor in list_sensors:
    print(f'{sensor.name}: {sensor.get_current_lux()} lux')
```

There is likely only ever to be a single sensor, so `macals.find_sensor()` is probably fine to use, which just returns the first.

```python
from macals import find_sensor

sensor = find_sensor()
print(f'{sensor.name}: {sensor.get_current_lux()} lux')
```

You can also just use the `LightSensor` class if you know the service name:

```python
from macals import LightSensor

sensor = LightSensor('AppleSPUVD6286')
print(f'{sensor.name}: {sensor.get_current_lux()} lux')
```
