from pydantic import BaseModel, Field

class DeviceAuthRequest(BaseModel):
    device_id: str = Field(min_length=3, max_length=64)
    api_key: str = Field(min_length=16, max_length=128)

class TokenResponse(BaseModel):
    access_token: str
    token_type: str = "bearer"
    expires_in_minutes: int = 60*24  # 24 horas