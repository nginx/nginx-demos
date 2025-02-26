function parseRequestBody(r) {
    try {
        if (r.variables.request_body) {
            JSON.parse(r.variables.request_body);
        }
        return r.variables.upstream;
    } catch (e) {
        r.error('JSON.parse exception');
        return '127.0.0.1:10415';
    }
}

export default { parseRequestBody };
