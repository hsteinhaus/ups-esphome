# SPDX-License-Identifier: GPL-2.0-only
import esphome.codegen as cg
from esphome.components import web_server_base
from esphome.components.web_server_base import CONF_WEB_SERVER_BASE_ID
import esphome.config_validation as cv
from esphome.const import CONF_FILE, CONF_ID, CONF_URL

AUTO_LOAD = ["web_server_base"]

CONF_CONTENT_TYPE = "content_type"

agent_doc_ns = cg.esphome_ns.namespace("agent_doc")
AgentDoc = agent_doc_ns.class_("AgentDoc", cg.Component)

# Serves one file from the repo at a fixed URL, so the firmware carries the
# documentation that matches it.
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AgentDoc),
        cv.GenerateID(CONF_WEB_SERVER_BASE_ID): cv.use_id(web_server_base.WebServerBase),
        cv.Required(CONF_FILE): cv.file_,
        cv.Optional(CONF_URL, default="/agent.md"): cv.string_strict,
        cv.Optional(CONF_CONTENT_TYPE, default="text/markdown; charset=utf-8"): cv.string_strict,
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    paren = await cg.get_variable(config[CONF_WEB_SERVER_BASE_ID])
    data = config[CONF_FILE].read_bytes()
    name = f"{config[CONF_ID]}_data"
    cg.add_global(cg.RawExpression(f"static const uint8_t {name}[] = {{{', '.join(str(b) for b in data)}}}"))
    var = cg.new_Pvariable(
        config[CONF_ID],
        paren,
        cg.RawExpression(name),
        len(data),
        config[CONF_URL],
        config[CONF_CONTENT_TYPE],
    )
    await cg.register_component(var, config)
