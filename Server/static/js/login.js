// Solo lógica visual. NADA de fetch ni tokens.
document.addEventListener('DOMContentLoaded', () => {
    const form = document.querySelector('form');
    
    form.addEventListener('submit', () => {
        // Solo para feedback visual
        const btn = form.querySelector('button');
        btn.textContent = "Entrando...";
        btn.disabled = true;
        // NO ponemos event.preventDefault()
        // Dejamos que el formulario viaje al servidor
    });
});