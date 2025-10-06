import matplotlib.pyplot as plt
import numpy as np

# Datos actualizados
num_procesos = [1, 2, 4, 6, 8, 10, 15]
tiempo_total = [0.013602, 0.006759, 0.005774, 0.004213, 0.003978, 0.005009, 0.004441]

# Crear un rango consecutivo para las barras
x = np.arange(len(num_procesos))

# Crear figura y barras con escala de amarillos
plt.figure(figsize=(9, 5))
bars = plt.bar(
    x,
    tiempo_total,
    color=plt.cm.YlOrBr(np.linspace(0.4, 1, len(num_procesos))),  # 🟡 Escala de amarillos
    edgecolor='black',
    linewidth=1,
    width=0.8
)

# Título y etiquetas
plt.title("Los Miserables", fontsize=16, fontweight='bold', color="#B7950B")
plt.xlabel("Nº de Procesos", fontsize=12)
plt.ylabel("Tiempo Total (s)", fontsize=12)

# Etiquetas encima de cada barra
for i, bar in enumerate(bars):
    plt.text(
        bar.get_x() + bar.get_width()/2,
        bar.get_height() + 0.0003,
        f"{tiempo_total[i]:.6f}",
        ha='center',
        va='bottom',
        fontsize=10,
        color="#7D6608"
    )

# Eje X con nombres exactos
plt.xticks(x, num_procesos, fontsize=10)
plt.yticks(fontsize=10)

# Fondo y cuadrícula estética
plt.gca().set_facecolor('#fffbea')  # 🌼 fondo crema suave
plt.grid(axis='y', linestyle='--', alpha=0.5)
plt.box(False)
plt.tight_layout()

plt.show()
