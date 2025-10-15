# producto_UI_SistemasOperativos

# Mini-Shell POSIX / C++

## Descripción
Un Mini-shell es un intérprete de comandos desarrollado en C++ que en el que puede simular funcionalidades básicas y avanzadas de una shell POSIX. Permitiendo ejecutar programas, redirecciones, pipes, tareas en segundo plano y comandos internos, con un prompt personalizado. Además, incluye instrumentación de memoria y manejo de señales.

---

## Requerimientos
- Sistema operativo tipo Unix (Ubuntu, Debian, Fedora, etc.)
- Compilador C++ con soporte C++17 (`g++`)
- Librerías POSIX estándar (`pthread`, `unistd.h`, `fcntl.h`, etc.)

---

## Instrucciones de ejecución

Para poder ejecutar el mini.shell, se debera abrir una terminal y poder ejecutar el siguiente comando:

**./mini_shell**

## Especificaciones Funcionales

### -Prompt personalizado

**mini-shell>**

### Resolucion de rutas

- Comando absoluto:
	**/bin/ls**

- Comando relativo:
	**ls**

### Funciones de ayuda

	**help**

### Manejo de errores

- Comando inexistentes:
	**./archivo_X** -> (saldra el siguiente mensaje: commando no encontrado o sin permisos)

### Ejecucion mediante procesos

Ejemplo con sleep para bloquear la shell en foreground:

	**sleep 20**
	**sleep 20 &**

(Si ejecutamos el sleep 20 sin el &, el shell se bloquea esperando al hijo y el prompt estara de vuelta después de 20 s. Al ejecutar el sleep 20 con &, la shell devolvera el prompt inmediatamente y registra el job en bgjobs.)

- al ejecutar el sleep 20 sin el &, en otra terminal realizaremos las siguientes ejecuciones:

	**pidof mini_shell**
	**MS_PID=$(pidof mini_shell)**
	**echo $MS_PID**
	**ps -f --forest -p $MS_PID**
	**ps -ef | grep '[s]leep 20'** Ó **pstree -p $MS_PID**-> (una vez implementado lo anterior, pasamos a ejecutar el sleep 20 sin & y luego ejecutamos ejecutar el siguiente comando que mostrara al sleep)

- al ejecutar el sleep 20 con el &, en la misma terminal ejecutamos lo siguiente:

	**jobs** -> (es para mostrar tareas en segundo plano '&')

### Redirección de salida (>, < y >>)

- Truncar archivo:

	echo primero > f.txt
	echo segundo > f.txt
	cat f.txt
	-> Resultado: segundo

- Append:

	echo a > f.txt
	echo b >> f.txt
	cat f.txt
	-> Resultado:
		 a
		 b

- Ejemplo simple:

	echo hola > salida.txt
	cat salida.txt
	-> Resultado: hola
	
- Otros:
	echo -e "pera\nmanzana\nuva" > datos.txt
	sort < datos.txt  (abrira datos.txt en modo lectura)

	echo "nuevo registro" >> log.txt
	sort < log.txt
	
### Uso de Pipes (|) simples

	**ls | grep cpp**
	**ls src/ | grep cpp**
	**cat include/shell.h | grep struct**
	**ps aux | grep bash**

### Concurrencia con hilos

	**parallel ls; sleep 2; echo Terminado**
	**parallel ls; pwd; date**

### Comando de Salida

	**salir**
