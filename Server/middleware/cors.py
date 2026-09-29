from fastapi.middleware.cors import CORSMiddleware

def setup_cors(app):
    """Configura CORS (⚠️ Solo para desarrollo)"""
    app.add_middleware(
        CORSMiddleware,
        allow_origins=[
            "http://127.0.0.1:5000",
            "http://10.25.186.94:5000",
            "http://localhost:5000",
            "https://127.0.0.1:5000",
            "https://localhost:5000",
        ],
        allow_credentials=True,
        allow_methods=["GET", "POST", "PUT", "DELETE", "OPTIONS"],
        allow_headers=["*"],
    )